# EtherCAT Motor Demo

基于 IgH EtherCAT Master 和 kaylordut 的电机控制示例，针对 **Renesas EtherCAT RA8T2 CiA402 2port** 伺服驱动器，使用 CSP（周期同步位置）模式 + DC（分布式时钟）同步实现电机持续转动。

## 安装驱动和应用

### 添加私有 APT 源
```bash
cat << 'EOF' | sudo tee /etc/apt/sources.list.d/kaylordut.list 
deb [arch=arm64 signed-by=/etc/apt/keyrings/kaylor-keyring.gpg] http://apt.kaylordut.cn/kaylordut/ kaylordut main
EOF
sudo mkdir /etc/apt/keyrings -pv
sudo wget -O /etc/apt/keyrings/kaylor-keyring.gpg http://apt.kaylordut.cn/kaylor-keyring.gpg
sudo apt update
```

### 检查内核版本并安装驱动
```bash
uname -a
# Linux ai-hunter 6.1.43-rt14-rockchip-rk3588 ... aarch64 GNU/Linux

apt policy ethercat-module
# 按输出选择与当前内核版本匹配的驱动包
```
```bash
# 注意：必须与当前内核版本完全匹配
sudo apt install -y ethercat-module=6.1.43-rt14-rockchip-rk3588 # 内核为 6.1.43-rt14-rockchip-rk3588 时
sudo apt install -y ethercat-module=5.10.160-rt89-rockchip-rk3588 # 内核为 5.10.160-rt89-rockchip-rk3588 时
```
> 请务必核对内核版本

### 安装应用
```bash
sudo apt install -y ethercat-master
```

## 编译

所有构建产物都在 `build/` 目录下，不污染源码目录：

```bash
mkdir -p build && cd build
cmake ..
make                     # 编译全部目标
make motor_continuous    # 只编译 motor_continuous
```

## EtherCAT 从站信息

当前从站（`ethercat slaves`）：
```
0  0:0  OP  +  Renesas EtherCAT RA8T2 CiA402 2port
```

从站标识：
- Vendor ID: `0x00000766`
- Product code: `0x00000802`
- Revision number: `0x00000200`

`ethercat cstruct` 输出的 PDO 映射（CSP 模式）：

```c
/* Master 0, Slave 0, "Renesas EtherCAT RA8T2 CiA402 2port"
 * Vendor ID:       0x00000766
 * Product code:    0x00000802
 * Revision number: 0x00000200
 */

ec_pdo_entry_info_t slave_0_pdo_entries[] = {
    {0x6040, 0x00, 16}, /* Control Word */
    {0x6072, 0x00, 16}, /* Max Torque */
    {0x607a, 0x00, 32}, /* Target Position */
    {0x60b0, 0x00, 32}, /* Position Offset */
    {0x60b1, 0x00, 32}, /* Velocity Offset */
    {0x60b2, 0x00, 16}, /* Torque Offset */
    {0x60b8, 0x00, 16}, /* Touch Probe Function */
    {0x60e0, 0x00, 16}, /* Positive Torque Limit Value */
    {0x60e1, 0x00, 16}, /* Negative Torque Limit Value */
    {0x60fe, 0x00, 32}, /* SubIndex 000 */
    {0x603f, 0x00, 16}, /* Error Code */
    {0x6041, 0x00, 16}, /* Status Word */
    {0x6064, 0x00, 32}, /* Position Actual Value */
    {0x606c, 0x00, 32}, /* Velocity Actual Value */
    {0x6077, 0x00, 16}, /* Torque Actual Value */
    {0x60b9, 0x00, 16}, /* Touch Probe Status */
    {0x60ba, 0x00, 32}, /* Touch probe position 1 positive value */
    {0x60bb, 0x00, 32}, /* Touch probe position 1 negative value */
    {0x60f4, 0x00, 32}, /* Following Error Actual Value */
    {0x60fd, 0x00, 32}, /* Digital Inputs */
};

ec_pdo_info_t slave_0_pdos[] = {
    {0x1601, 10, slave_0_pdo_entries + 0}, /* csp RxPDO */
    {0x1a01, 10, slave_0_pdo_entries + 10}, /* csp TxPDO */
};

ec_sync_info_t slave_0_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, slave_0_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT, 1, slave_0_pdos + 1, EC_WD_DISABLE},
    {0xff}
};
```
> 程序中实际只用到了 4 个 PDO 项：`0x6040`(控制字)、`0x607a`(目标位置)、`0x6064`(实际位置)、`0x6041`(状态字)。

## motor_continuous 程序

基于 `test_dc_scan` 验证过的配置（DC 激活码 `0x0301`，1ms 周期 / 1000Hz），实现电机持续转动的测试程序。目标位置采用**往返策略**：在 `±10000`（±1万）边界之间来回移动，每周期步进 `±100`，避免位置计数器长时间运行后溢出。

### 运行

```bash
# 必须用 sudo：CSP+DC 模式需要实时调度 (SCHED_FIFO)，否则从站会掉状态
sudo ./build/motor_continuous <duration_ms>
```

参数说明：
- `duration_ms > 0`：运行指定毫秒后自动退出（如 `10000` = 10秒）
- `duration_ms = 0`：**无限运行**，只有按 `Ctrl+C` 或收到 `SIGTERM` 才退出（退出前会反激活 master）

```bash
sudo ./build/motor_continuous 10000   # 运行 10 秒
sudo ./build/motor_continuous 0       # 无限运行，Ctrl+C 退出
```

### 为什么必须 sudo

程序启用了实时调度（`SCHED_FIFO` 最高优先级 + `mlockall` 锁定内存 + 栈预锁定）。CSP+DC 模式对周期精度要求极高，普通用户态进程会被调度抢占导致周期抖动，从站收不到及时的周期数据，**watchdog 超时后会从 OP 掉到 SAFEOP / PREOP / INIT**。非 root 运行时 `sched_setscheduler` 会失败，日志会有警告。

### 打印格式说明

每个周期（每秒打印一次）输出格式：
```
[t= 100ms] AL=OP Status=[Rdy On Ena Vol] pos=9976 target=10076 vel=120 tor=15 ↑ ENABLED
```
- `pos`：当前位置（电机反馈 0x6064）
- `target`：目标位置（pos ± 100）
- `vel`：实际速度（电机反馈 0x606c）
- `tor`：实际扭矩（电机反馈 0x6077，单位 0.001 Nm）
- `↑/↓`：当前移动方向

状态缩写（程序启动和结束各打印一次图例）：
- **AL 状态**：`INIT` / `PREOP` / `SAFEOP` / `OP`
- **Status**：`Rdy`(Ready) `On`(Switched On) `Ena`(Enabled) `Flt`(Fault) `Vol`(Voltage) `Dis`(Disabled) `Tgt`(Target Reached) `Ign`(Ignoring Target)

到达位置限位（±1万）方向切换时，会打印一条 `[WARN]` 日志。
