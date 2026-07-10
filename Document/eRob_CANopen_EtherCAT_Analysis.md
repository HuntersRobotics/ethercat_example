# eRob 关节模组 EtherCAT 通信分析

> 基于 eRob CANopen and EtherCAT User Manual v1.9 与项目代码的交叉分析

## 一、当前代码使用的运行模式：CSP（周期同步位置模式）

### 1.1 判定依据

| 判定维度 | CSP 模式特征（文档 5.5 节） | 代码实际行为 | 匹配 |
|---------|---------------------------|-------------|------|
| 模式选择 | 0x6060 = 0x08 | eRob 默认上电即为 CSP 模式（文档 5.5 节默认 PDO 映射即为 CSP 模式），代码未主动切换 | ✅ |
| 轨迹规划 | **主站**负责轨迹规划，每周期发送目标位置 | `RunOnce()` 每周期计算 `position - 100` 并写入 0x607A | ✅ |
| RxPDO 映射 | 0x6040（Control Word）+ 0x607A（Target Position） | `pdo_entry_regs[]` 精确映射了这两个对象 | ✅ |
| TxPDO 映射 | 0x6041（Status Word）+ 0x6064（Position Actual Value） | `pdo_entry_regs[]` 精确映射了这两个对象 | ✅ |
| 控制字序列 | Shutdown(0x06) → Switch on(0x07) → Enable operation(0x0F) | `InitializeDevices()` 严格按照此序列执行 | ✅ |
| 目标位置含义 | 绝对位置，目标位置窗口判断到达 | 代码用 Bit 10（Target reached）判断是否到达，然后更新目标 | ✅ |
| 周期性 | 每个通信周期都要发送目标位置 | while 循环中每 1ms 周期发送 | ✅ |

### 1.2 与轮廓位置模式（PP, 0x01）的对比

| 对比项 | CSP（代码实际使用） | PP（轮廓位置模式） |
|--------|-------------------|-------------------|
| 轨迹规划者 | **主站**（代码在 `RunOnce()` 中计算路径） | **驱动器内部**（驱动器根据 0x6081/0x6083/0x6084 参数规划） |
| 0x607A 作用 | 每个周期由主站实时更新 | 一次写入，驱动器自行运动到目标 |
| 额外参数 | 仅需 0x607A | 还需配置 Profile Velocity (0x6081)、Acceleration (0x6083)、Deceleration (0x6084) |
| 控制字特殊性 | CSP 模式对 Bit4-6/8/9 无特殊要求（reserved） | PP 需设 Bit4 (new set-point)、Bit5 (change set immediately)、Bit6 (absolute/relative) |
| 适用场景 | 主站需要实时干预轨迹（如视觉伺服、力控） | 简单的点到点运动 |

**结论：代码明确使用的是 CSP（周期同步位置模式），且可能是零差电机上电的默认模式。**

---

## 二、代码架构分析

### 2.1 整体结构

```
main.cpp          → 实时主循环，SCHED_FIFO 调度，1ms 周期
  ├── App::Config()           → 配置从站参数和 PDO 映射
  ├── App::CheckMasterState() → 检查 EtherCAT 主站状态
  ├── App::InitializeDevices() → 执行 CiA 402 状态机初始化
  └── App::RunOnce()          → 每周期接收/发送过程数据

ethercat_node.cpp → IgH EtherCAT Master 封装层
  ├── Initialize()       → 请求主站、创建域、配置从站、激活
  ├── CheckMasterState()     → 主站状态监控
  ├── CheckSalveConfigStates() → 从站 AL 状态监控
  └── CheckoutDomainState() → 过程数据域状态监控
```

### 2.2 PDO 映射表

**RxPDO (主站 → 从站):**

| 偏移 | 对象字典 | 名称 | 位宽 | 用途 |
|------|---------|------|------|------|
| 0 | 0x607A:00 | Target Position | 32-bit | 主站发送的目标位置（绝对位置） |
| 4 | 0x6040:00 | Control Word | 16-bit | 状态机控制字 |

**TxPDO (从站 → 主站):**

| 偏移 | 对象字典 | 名称 | 位宽 | 用途 |
|------|---------|------|------|------|
| 0 | 0x6064:00 | Position Actual Value | 32-bit | 当前实际位置反馈 |
| 4 | 0x6041:00 | Status Word | 16-bit | 状态机状态字反馈 |

### 2.3 从站识别信息

| 参数 | 值 | 含义 |
|------|-----|------|
| Vendor ID | 0x5a65726f | "ZeroErr" (零差云控) |
| Product Code | 0x00029252 | eRob 关节模组 |
| Alias | 0 | |
| Position | 0 | 总线上第 0 个从站 |

---

## 三、CiA 402 状态机流程（对应文档 5.4 节 PDS FSA）

### 3.1 初始化序列

代码中 `InitializeDevices()` 严格按照文档表 5-5 的控制字时序：

```
状态字 Bit0-3         控制字      　含义
─────────────        ──────      ──────────
0x0100 (xxxx x001)   → 0x06     Shutdown（关闭）
0x0031 (xxxx x011)   → 0x07     Switch on（准备使能）
0x0033 (xxxx x011)   → 0x0F     Enable operation（使能）
0x0037 (xxxx x111)   初始化完成    Operation enabled（伺服运行）
```

### 3.2 代码中状态字的关键位检查

| Bit | 名称 | 代码检查方式 | 含义 |
|-----|------|------------|------|
| Bit 0-3 | 状态机核心位 | `status & 0x0F == 0x07` | 确认进入 Operation enabled 状态 |
| Bit 3 | Fault | `status & 0x0008` | 有故障 → 发送 0x80 复位 |
| Bit 4 | Voltage enabled | `status & 0x10` | 母线电压正常 |
| Bit 6 | Switch on disabled | `status & 0x50 == 0x50` | 伺服无故障 + 电压正常 |
| Bit 10 | Target reached | `status & 0x0400` | 目标位置已到达 |
| Bit 9 | Remote | status 的一部分 | 现场总线控制（默认 1） |

### 3.3 故障处理逻辑

```cpp
if (status & 0x0008) {    // Bit 3 = Fault
    EC_WRITE_U16(control_word, 0x80);  // Bit 7 上升沿 → Fault Reset
}
```

对应文档 5.5 节控制字 Bit 7 的定义：上升沿触发故障复位。

---

## 四、CSP 模式下的运动控制逻辑

### 4.1 轨迹规划

当前代码实现的是**简单的匀速负向运动**：

```cpp
// RunOnce() 中的控制逻辑
if (status & 0x0400) {  // Bit 10: Target reached
    // 每次到达目标后，将目标位置减 100
    EC_WRITE_S32(target_position, position - 100);
}
```

轨迹特征：
- 每次目标到达后，向下一个位置点移动（-100 counts）
- 由主站在每个通信周期（1ms）发送更新后的 0x607A
- 电机内部执行位置环 + 速度环 + 扭矩环的闭环控制

### 4.2 控制流程图

```
┌──────────────────────────────────────────────────┐
│                实时循环 (1ms)                       │
│                                                    │
│  ecrt_master_receive()    ← 接收 EtherCAT 帧        │
│         ↓                                          │
│  ecrt_domain_process()    ← 交换 PDO 数据            │
│         ↓                                          │
│  读取 Status Word (0x6041)                          │
│  读取 Position Actual (0x6064)                      │
│         ↓                                          │
│  ┌──────────────────────────┐                      │
│  │ 状态机 OK + Bit10=1 ?     │                      │
│  │ YES → 更新目标位置 -100   │                      │
│  │ NO  → 保持当前目标位置    │                      │
│  └──────────────────────────┘                      │
│         ↓                                          │
│  写入 Target Position (0x607A)                      │
│  写入 Control Word (0x6040)                         │
│         ↓                                          │
│  ecrt_domain_queue()                               │
│  ecrt_master_send()       ← 发送 EtherCAT 帧        │
└──────────────────────────────────────────────────┘
```

---

## 五、文档中所有可用模式总览

| 值 | 模式 | 中文名称 | 状态 |
|----|------|---------|------|
| 0x01 | Profile Position (PP) | 轮廓位置模式 | ✅ 支持 |
| 0x03 | Profile Velocity (PV) | 轮廓速度模式 | ✅ 支持 |
| 0x04 | Profile Torque (PT) | 轮廓扭矩模式 | ✅ 支持 |
| 0x06 | Homing (HM) | 回零模式 | ❌ 暂不支持 |
| 0x07 | Interpolated Position (IP) | 位置插补模式 | ❌ 暂不支持 |
| 0x08 | Cyclic Synchronous Position (CSP) | 周期同步位置模式 | ✅ **当前使用** |
| 0x09 | Cyclic Synchronous Velocity (CSV) | 周期同步速度模式 | ✅ 支持 |
| 0x0A | Cyclic Synchronous Torque (CST) | 周期同步扭矩模式 | ✅ 支持 |

### 5.1 各模式简要对比

| 模式 | 轨迹规划 | 控制环 | 典型应用场景 |
|------|---------|--------|------------|
| **CSP** | 主站 | 驱动器内位置/速度/扭矩 | 实时轨迹跟踪、视觉伺服 |
| **PP** | 驱动器 | 驱动器内位置/速度/扭矩 | 点到点定位 |
| **CSV** | 主站 | 驱动器内速度/扭矩 | 速度控制、传送带同步 |
| **PV** | 驱动器 | 驱动器内速度/扭矩 | 简单速度控制 |
| **CST** | 主站 | 驱动器内扭矩 | 力/扭矩控制 |

---

## 六、代码与文档的关键对应点

| 文档章节 | 内容 | 代码对应 |
|---------|------|---------|
| 5.4 PDS FSA | 状态机切换流程 | `InitializeDevices()` 中的控制字序列 |
| 5.4.1 状态字 0x6041 | Bit 定义 | `user_data.status_word` 的位检查 |
| 5.4.2 控制字 0x6040 | 控制字定义 | 0x06/0x07/0x0F/0x80 的使用 |
| 5.5 CSP 模式 | CSP 操作步骤 | `RunOnce()` 周期写入 0x607A |
| 5.5 表 5-8 | CSP 关联对象 | PDO 映射中使用的 4 个对象 |
| 4.5 DC 模式 | Sync0 同步 | readme 中 ethercat cstruct 的 sync 配置 |
| 8.x 对象字典 | 0x607A/0x6040/0x6064/0x6041 | `pdo_entry_regs[]` 数组 |

---

## 七、总结

当前代码实现了一个**基于 IgH EtherCAT Master 的零差 eRob 关节模组 CSP（周期同步位置模式）控制程序**，核心特征：

1. **实时性**：SCHED_FIFO 实时调度 + 内存锁定 + 1ms 严格周期
2. **状态机**：严格按照 CiA 402 (DSP 402) 标准实现 FSA 状态切换
3. **运动控制**：主站负责简单轨迹规划，每周期通过 PDO 下发目标位置
4. **当前策略**：匀速负向运动（每到达目标后 -100 counts），可用于简单的电机测试/演示
5. **扩展性**：`EthercatNode` 封装了通用的 IgH Master 操作，可方便扩展更多从站或更复杂的运动轨迹
