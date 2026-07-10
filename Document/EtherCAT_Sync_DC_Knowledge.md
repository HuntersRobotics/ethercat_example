# EtherCAT 同步机制（DC / Sync0）知识手册

> 本文档系统讲解 EtherCAT 的同步机制，结合本项目（Renesas RA8T2 / 汇川 Dex-hand 灵巧手）的实际调试经验整理，供后续开发参考。

Copyright (c) 2026 kaylorchen
SPDX-License-Identifier: TBD

---

## 目录

1. [为什么需要同步](#1-为什么需要同步)
2. [分布式时钟 DC](#2-分布式时钟-dc)
3. [Sync0 / Sync1 信号](#3-sync0--sync1-信号)
4. [同步模式 Sync Mode](#4-同步模式-sync-mode)
5. [同步的配置要素与寄存器](#5-同步的配置要素与寄存器)
6. [assign-activate 详解](#6-assign-activate-详解)
7. [周期同步模式（CSP/CSV/CST）为什么依赖 Sync0](#7-周期同步模式cspcsvcst为什么依赖-sync0)
8. [完整同步时序](#8-完整同步时序)
9. [IgH EtherCAT Master 相关 API](#9-igh-ethercat-master-相关-api)
10. [本项目（RA8T2）的同步问题诊断](#10-本项目ra8t2的同步问题诊断)
11. [诊断命令速查](#11-诊断命令速查)
12. [常见状态字 / 错误码](#12-常见状态字--错误码)

---

## 1. 为什么需要同步

EtherCAT 是**分布式系统**：一个主站带多个从站，主站每周期（通常 1ms）发出一帧数据，该帧"环游"所有从站后返回。在这个架构下存在两个时序问题：

- **数据到达各从站有先后**：信号在线缆和从站内部传播需要时间（纳秒级），每个从站收到数据的时刻略有差异。
- **从站处理/输出时刻不统一**：如果不加约束，多个从站会在各自收到数据的不同时刻驱动执行机构，多轴协调运动就会"参差不齐"。

运动控制（尤其 CSP/CSV/CST 周期同步模式）要求**微秒级**的确定性时序。因此需要一个机制让所有从站**在同一时刻**采样输入、施加输出——这就是 EtherCAT 的同步机制。

---

## 2. 分布式时钟 DC

每个从站的 ESC（EtherCAT Slave Controller，EtherCAT 从站控制器）芯片内部都有一个**本地时钟**（64 位纳秒计数器）。DC（Distributed Clocks，分布式时钟）机制完成三件事：

1. **选举参考时钟**（Reference Clock）：主站在总线上选择一个支持 DC 的从站作为参考时钟，通常是**第一个**具备 DC 能力的从站（本项目中即 slave 0）。
2. **从站时钟对齐**：其他从站的本地时钟通过补偿传播延迟，对齐到参考时钟。
3. **参考时钟校准**：主站把参考时钟校准到主站自己的系统时间。

对齐完成后，总线上所有从站拥有**统一的时间基准**。

**关键 ESC 寄存器：**

| 寄存器 | 含义 |
|--------|------|
| `0x0910` | DC 系统时间（64 位，纳秒） |
| `0x0918` | 上一次写入的 DC 系统时间 |
| `0x0920–0x0927` | DC 端口接收时间戳（用于延迟补偿） |

主站通过 `ecrt_master_application_time()` 每周期告知从站当前系统时间，用于 DC 锁相。

---

## 3. Sync0 / Sync1 信号

仅有统一时钟还不够，还需要一个**"开火"信号**让所有从站在同一时刻动作。DC 单元可生成两个硬件脉冲信号：

| 信号 | 作用 |
|------|------|
| **Sync0** | 主周期中断信号。从站在这一刻**采样输入 + 施加输出**，所有从站同时发生 |
| **Sync1**（可选） | 比 Sync0 提前一点触发，用于从站做预处理（如轨迹计算） |

**关键特性**：Sync0 是从站**本地 DC 硬件**按配置的 cycle time 自主产生的，一旦配好，**不依赖主站每周期发送触发**，因此精度极高（纳秒级）。

---

## 4. 同步模式 Sync Mode

从站的 PDO（过程数据）处理时机由同步模式决定，通过对象字典 `0x1C32`（RxPDO）和 `0x1C33`（TxPDO）的 subindex 1 配置：

| 值 | 模式 | 行为 |
|----|------|------|
| 0 | **Free run（自由运行）** | 收到数据帧就立即处理，不等待同步信号 |
| 1 | **Synchronous（同步）** | 等待 Sync 信号（Sync0/Sync1）才处理 |
| 2 | DC Synchronous（细化） | 同步模式的细分 |

> **本项目实测**：RA8T2 的 `0x1C32:01 = 1`（同步模式），因此它**必须等 Sync0 才会采样目标位置**。这是它没配 Sync0 时忽略目标位置（status bit12=1）的直接原因。

---

## 5. 同步的配置要素与寄存器

配置同步需要主站、从站双方协同：

### 主站侧（每周期执行）

```
① ecrt_master_application_time()   —— 告知从站"现在几点"
② ecrt_master_sync_reference_clock() —— 校准参考从站的 DC 时钟
③ ecrt_master_sync_slave_clocks()    —— 让其他从站时钟跟上参考时钟
```

### 从站侧（SAFEOP 转换时配置一次）

| 要素 | 寄存器 | 含义 | 由谁决定 |
|------|--------|------|---------|
| Sync0/Sync1 激活 + 握手码 | `0x0980` | 启用 DC 中断 | **ESI xml（厂家）** ★ |
| Sync0 脉冲长度 | `0x0981–0x0982` | 中断脉冲宽度 | ESI xml |
| SYNC0/SYNC1 起始时间 | `0x0985–0x0990` | 首次触发时刻 | 主站 |
| Sync0 周期 | `0x09A0` | 多久触发一次（= 主站周期） | 主站 |
| Sync0 偏移 | `0x09A4` | 相对参考时钟的偏移 | 主站（常为 0） |
| Sync1 周期 | `0x09A8` | Sync1 多久触发 | 主站 |

**核心结论**：除 `0x0980` 的握手码外，其余参数主站都能自行设定。而 `0x0980` 的值无法从设备运行时读出，**只能从厂家的 ESI xml 获取**——这也是本项目卡住的根因。

---

## 6. assign-activate 详解

`0x0980` 是一个 16 位值（在 ESI 文件中对应 `<Dc><Op><AssignActivate>` 节点），它干两件事：

- **告诉从站 DC 单元**："启用 Sync0（bit0）、启用 Sync1（bit1）"。
- **握手/编码**：不同从站固件对该值的编码定义不同，因此**没有通用值**。

常见值 `0x0300`（IgH 示例常用），但具体到每个从站，必须以厂家 ESI 中的值为准。这是为什么：

- 主站无法通过 SDO/SII/寄存器从从站读出该值（协议设计如此）；
- `ethercat xml` 反向生成的 ESI xml 中 `<Dc>` 节点是空的；
- 只有厂家用 SSC Tool 生成固件时导出的那份 ESI xml 才包含真实值。

---

## 7. 周期同步模式（CSP/CSV/CST）为什么依赖 Sync0

CiA 402 的周期同步模式（CSP=位置、CSV=速度、CST=扭矩）工作流程：

```
主站每 1ms 发送一次目标值（目标位置/速度/扭矩）
        ↓ 数据帧环游到从站
从站收到并缓存，但不立即使用
        ↓ 等待下一个 Sync0 中断（与主站周期对齐）
从站在 Sync0 时刻：采样目标 → 运行控制环 → 输出
```

**没有 Sync0，从站永远不知道"何时采样目标"**，于是：

- 目标值被写入对象字典（0x607A 等能读到），但驱动器不采纳；
- status word 的 **bit12 = 1**（CSP 模式下含义为 "Target position ignored"，目标位置被忽略）；
- 电机不动作，位置/速度反馈恒为 0。

**对照模式**：PP（轮廓位置）、PV（轮廓速度）、PT（轮廓扭矩）是**非周期**模式，驱动器内部自己规划轨迹、维持目标，**不依赖 Sync0**——但需要驱动器支持（查 `0x6502`）。

---

## 8. 完整同步时序

理想情况下（Sync0 已正确配置）：

```
t=0.000ms   主站发送数据帧（含本周期目标位置）
t=0.0xxms   帧到达从站，从站缓存（不立即用）
t=1.000ms   ← Sync0 中断！所有从站同时采样 + 执行
t=1.000ms   主站发送下一帧
t=2.000ms   ← 下一个 Sync0
...
```

- 主站发帧时刻与从站 Sync0 执行时刻**故意错开**；
- **shift time** 就是调节这个错开量的参数，保证从站执行时使用的是"最新且已稳定"的数据；
- 若 shift time 配置不当，会导致从站用到上一周期的旧数据，影响控制性能。

---

## 9. IgH EtherCAT Master 相关 API

```c
/* 激活从站的 DC（在 master_activate 之前调用）*/
ecrt_slave_config_dc(sc,
    assign_activate,     // 0x0980 握手码（来自 ESI）
    sync0_cycle_time,    // Sync0 周期（纳秒），= 主站周期
    sync0_shift_time,    // Sync0 偏移（纳秒）
    sync1_cycle_time,    // Sync1 周期
    sync1_shift_time);   // Sync1 偏移

/* 周期任务中（每个周期调用）*/
struct timespec t;
clock_gettime(CLOCK_MONOTONIC, &t);
ecrt_master_application_time(master, TIMESPEC2NS(t));
ecrt_master_sync_reference_clock(master);
ecrt_master_sync_slave_clocks(master);
```

**正确调用顺序**（周期任务）：

```
application_time + sync_reference_clock + sync_slave_clocks
   → ecrt_domain_queue + ecrt_master_send（发帧）
   → 等待下一周期
   → ecrt_master_receive + ecrt_domain_process（收帧处理）
```

---

## 10. 本项目（RA8T2）的同步问题诊断

### 设备信息

- 设备：Renesas EtherCAT RA8T2 CiA402 2port（汇川 Dex-hand 17-axis 灵巧手）
- Vendor ID: `0x00000766`，Product Code: `0x00000802`
- 同步模式：`0x1C32:01 = 1`（DC 同步），设备名含 "DC Synchron"
- 支持模式：`0x6502 = 0x80`（按本手册位定义 = bit7 = CSP，**非**标准 CiA402 的 bit5）

### 已验证正常的部分

- EtherCAT 通信：能稳定进入 OP 状态
- CiA 402 PDS 状态机：程序带 OP 后，`4641(0x1221) → 控制字 0x0F → 4663(0x1237)` 使能成功
- 目标位置写入：`0x607A` 能正确写入对象字典

### 未解决的核心问题

- **DC Sync0 未激活**：`0x0980 = 0`，IgH 的 `ecrt_slave_config_dc(0x0300)` 未真正配置（日志仅显示 "Clearing DC assignment"，无 "Configuring DC"）。
- 手动写 `0x0980=1`、`0x09A0=1000000` 可激活寄存器，但 Sync0 不完整（缺起始时间/相位），CSP 仍忽略目标（bit12=1）。
- **根因**：缺厂家的 ESI xml（`Renesas EtherCAT RA8T2 CiA402.xml`）中的 `AssignActivate` 值。

### 待办

1. 向汇川/瑞萨索取 ESI xml（在固件包的 Tool 文件夹，由 SSC Tool 生成）；
2. 解析 xml 中 `<Dc><Op><AssignActivate>` 值，替换 IgH 代码中的 `0x0300`；
3. 必要时补全完整 PDO 映射（xml 中的 RxPdo/TxPdo）。

---

## 11. 诊断命令速查

| 命令 | 用途 |
|------|------|
| `ethercat sdos -p 0` | 列出整个对象字典 |
| `ethercat upload -p 0 -t <type> <idx> <sub>` | 读单个对象（注意类型：uint8/16/32） |
| `ethercat download -p 0 -t <type> <idx> <sub> <val>` | 写单个对象 |
| `ethercat pdos -p 0` | PDO 映射 + SyncManager 配置（含 ControlRegister） |
| `ethercat cstruct -p 0` | 同上，导出为 C 代码 |
| `ethercat slaves -v` | 从站详情（DC 能力、邮箱、端口） |
| `ethercat sii_read -p 0` | 读 SII（EEPROM） |
| `ethercat reg_read -p 0 -t <type> <addr>` | 读 ESC 寄存器（如 `0x0980` DC 激活） |
| `ethercat reg_write -p 0 -t <type> <addr> <val>` | 写 ESC 寄存器 |
| `ethercat xml -p 0` | 反向生成 ESI xml（**注意：`<Dc>` 节点为空**） |
| `ethercat debug <0-2>` | 设置 IgH 调试日志级别 |
| `dmesg \| grep EtherCAT` | 查看内核中 IgH 主站日志 |

**关键 DC 寄存器速查：**

```bash
# DC Sync0 是否激活
sudo ethercat reg_read -p 0 -t uint16 0x0980
# Sync0 周期时间
sudo ethercat reg_read -p 0 -t uint32 0x09A0
# AL 状态与错误码
sudo ethercat reg_read -p 0 -t uint16 0x0130   # AL state
sudo ethercat reg_read -p 0 -t uint16 0x0134   # AL status code
```

---

## 12. 常见状态字 / 错误码

### status word (0x6041) 关键位

| bit | 含义 |
|-----|------|
| 0 | Ready to switch on |
| 1 | Switched on |
| 2 | Operation enabled（伺服使能） |
| 3 | Fault（故障） |
| 4 | Voltage enabled（母线电压正常） |
| 5 | Quick stop（=1 正常） |
| 6 | Switch on disabled（初始化完成标志） |
| 9 | Remote（远程控制） |
| 10 | Target reached（目标到达） |
| 12 | CSP 模式：Target position ignored（目标被忽略） |

### PDS 状态识别（看低 6 位 `status & 0x6F`）

| 状态字低字节 | PDS 状态 |
|---|---|
| 0x00 | 初始化 Not ready（bit0-6 全 0） |
| 0x40 | Switch on disabled（初始化完成） |
| 0x21 | Ready to switch on（= 十进制 4641 的低字节） |
| 0x23 | Switched on |
| 0x27 / 0x37 | Operation enabled（= 4663 的低字节 0x37） |

### AL status code (0x0134) 常见值

| 码 | 含义 |
|----|------|
| 0x0000 | 无错误 |
| 0x0011 | 无效的 PDO 映射 |
| 0x001A | DC：无效的 Sync 周期 |
| 0x001B | DC：Sync0 周期超范围 |
| 0x0032 | 看门狗测试失败 |

---

## 附：关键概念速记

- **DC**：给所有从站统一时钟的机制。
- **Sync0**：基于 DC 时钟的周期硬件中断，触发从站采样/执行。
- **Sync Mode（0x1C32:01）**：从站是否等 Sync 信号（1=等）。
- **assign-activate（0x0980）**：激活 Sync0 的握手码，**只能从 ESI xml 获取**。
- **CSP 必须 Sync0**：否则目标被忽略（bit12=1），电机不动。
- **本项目的卡点**：缺 ESI 中的 assign-activate 值。
