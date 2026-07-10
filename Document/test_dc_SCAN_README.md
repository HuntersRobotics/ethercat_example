# test_dc_scan 使用说明

## 概述

`test_dc_scan` 是一个 EtherCAT DC (Distributed Clock) 激活码测试工具，用于验证不同激活码配置下电机是否能正常工作。

## 功能

该工具对单个 AssignActivate(0x980) 值做一次完整的 CSP (Cyclic Synchronous Position) 模式实测：

- ✅ 配置 CSP 模式 + DC 分布式时钟
- ✅ 推进 CiA402 状态机到 Operation Enabled
- ✅ 写入目标位置并观察电机响应
- ✅ 输出详细的测试结果和结论

## 编译

```bash
cd build
cmake ..
make
```

编译后生成 `./build/test_dc_scan` 可执行文件。

## 用法

```bash
./test_dc_scan <assign_activate> [cycle_ns] [duration_ms]
```

### 参数说明

| 参数 | 说明 | 默认值 | 示例 |
|------|------|--------|------|
| `assign_activate` | DC 激活码 (16进制) | 0x0300 | `0x0301` |
| `cycle_ns` | DC 周期时间 (纳秒) | 1000000 | `1000000` |
| `duration_ms` | 测试持续时间 (毫秒) | 4000 | `5000` |

## 使用示例

### 1. 基本测试（使用默认值）

```bash
./test_dc_scan
```

### 2. 测试特定激活码

```bash
./test_dc_scan 0x0301
```

### 3. 自定义周期和持续时间

```bash
./test_dc_scan 0x0301 1000000 8000
```

### 4. 批量扫描不同激活码

使用提供的扫描脚本：

```bash
./scan_dc_codes.sh
```

该脚本会依次测试多个候选激活码：`0x0000 0x0100 0x0200 0x0300 0x0700 0x0301 0x0101 0x0331`

## 输出说明

### 运行过程输出

```
[t=00000ms] al=0x2 status=0x0000 pos=0
[t=  500ms] al=0x2 status=0x0000 pos=0
[INFO] 已使能, initial_pos=5940, target=6940
[t= 1000ms] al=0x8 status=0x1237 pos=5999 ENABLED
[t= 1500ms] al=0x8 status=0x1237 pos=6261 ENABLED
```

- `al` - AL 状态 (0x02=PREOP, 0x04=SAFEOP, 0x08=OP)
- `status` - CiA402 状态字 (0x1237=Operation enabled)
- `pos` - 实际位置值
- `ENABLED` - 电机已使能

### 最终结果输出

```
---------- 结果 ----------
AssignActivate   : 0x0301
AL state (末值)  : 0x8 (OP=YES)
Operation enabled: YES
Fault 发生过     : NO
安全停止触发     : NO
status word (末值): 0x1237
bit12 (CSP目标忽略): 1
Target Position  : 6940
Initial Position : 5940
Actual Position  : [5940 ~ 6813], travel = 873

>>> 结论: MOTOR_MOVED (travel=873) — 该激活码可能正确!
```

## 结果判断

### 成功指标

**MOTOR_MOVED** - 电机移动了
- ✅ `travel > 10` (位置实际变化)
- ✅ `Operation enabled: YES`
- ✅ 该激活码可能正确

### 部分成功

**BIT12_CLEAR** - Sync0 可能生效
- ⚠️ 进 OP 使能且 bit12=0 (目标未被忽略)

**OP_BUT_IDLE** - Sync0 未完全生效
- ⚠️ 进 OP 使能但 bit12=1 (目标被忽略)
- ⚠️ travel=0 或很小

### 失败

**NOT_WORKING** - 不工作
- ❌ 未进 OP 或未使能
- ❌ 发生故障

## 已验证的激活码

对于 **CN032-9 DC Servo Solution** 电机：

| 激活码 | 结果 | 说明 |
|--------|------|------|
| **0x0301** | ✅ MOTOR_MOVED | **推荐的激活码** |
| 0x0300 | ❌ NOT_WORKING | ESI 文件值，不工作 |
| 0x0700 | ❌ NOT_WORKING | 无法进 OP |

## 常见问题

### 1. 无法请求 master

```
[FATAL] request master 失败
```

**原因**：另一个 EtherCAT 程序正在运行

**解决**：停止其他程序
```bash
pkill -9 ethercat_example
pkill -9 test_dc_scan
```

### 2. 电机不移动

**可能原因**：
- 激活码不正确
- 电机未上电或未初始化
- PDO 配置错误

**解决**：
- 尝试已验证的激活码 `0x0301`
- 检查电机电源和编码器
- 重新上电复位电机位置

### 3. 卡在 PREOP (0x02)

**原因**：从站配置问题或缺少必要配置

**解决**：
- 检查设备是否在线
- 验证 Vendor ID 和 Product Code
- 确认 PDO 配置正确

## 安全机制

程序内置安全限位保护：

```cpp
static const int32_t kSafetyTravelLimit = 200000;
```

单次测试中位置累计行程超过 **200000** 时会自动停止，防止电机失控。

## 注意事项

1. **运行前停止其他 EtherCAT 程序**
2. **确保电机已正确上电和初始化**
3. **使用已验证的激活码 0x0301**
4. **观察电机实际转动情况**
5. **注意安全限位保护**

## 相关文档

- `EtherCAT_Sync_DC_Knowledge.md` - EtherCAT DC 同步原理
- `CN032-9 DC Servo Solution - Startup Manual.pdf` - 电机官方手册
- `scan_dc_codes.sh` - 批量扫描脚本
