# EtherCAT Motor Demo

基于 IgH EtherCAT Master 和 kaylordut 的电机控制示例，针对 **Renesas EtherCAT RA8T2 CiA402 2port** 伺服驱动器，使用 CSP（周期同步位置）模式 + DC（分布式时钟）同步实现电机持续转动。

## 安装 EtherCAT 主站（驱动 + 应用）

目标平台：Jetson Thor / JetPack 7.2（Ubuntu 24.04 noble），PREEMPT_RT 内核，Intel I225 网卡（igc 驱动）。

### 添加私有 APT 源

```bash
sudo mkdir -p /etc/apt/keyrings
curl -fsSL http://apt.renesas.kaylordut.com/renesas_pai-public-key.asc | \
    sudo gpg --dearmor -o /etc/apt/keyrings/renesas-pai.gpg
cat << 'EOF' | sudo tee /etc/apt/sources.list.d/renesas-pai.list
deb [arch=arm64 signed-by=/etc/apt/keyrings/renesas-pai.gpg] http://apt.renesas.kaylordut.com/ all stable
EOF
sudo apt update
```

### 检查内核版本

```bash
uname -r
# 6.8.12-1021-rt-tegra   ← PREEMPT_RT 内核（非 RT 为 6.8.12-1021-tegra）
cat /sys/kernel/realtime  # 1 = RT 内核
```

### 部署 EtherCAT 内核模块（ec_master / ec_igc / ec_generic）

模块 `.ko` 的 vermagic 必须与当前内核完全匹配（`6.8.12-1021-rt-tegra SMP preempt_rt ... aarch64`），
来自交叉编译产物（如 `deploy-6.8.12-1021-rt-tegra/ethercat-kos/`）：

```bash
MOD_VER=$(uname -r)   # 6.8.12-1021-rt-tegra
sudo mkdir -p /lib/modules/$MOD_VER/updates/ethercat
sudo cp ec_master.ko ec_igc.ko ec_generic.ko /lib/modules/$MOD_VER/updates/ethercat/
sudo depmod -a $MOD_VER
```

### 配置并启动主站（I225 → igc）

```bash
# /etc/ethercat.conf 由 ethercat-master 包安装，按目标网卡修改
sudo sed -i 's|^DEVICE_MODULES=.*|DEVICE_MODULES="igc"|' /etc/ethercat.conf
sudo sed -i 's|^MASTER0_DEVICE=.*|MASTER0_DEVICE="<I225-MAC>"|' /etc/ethercat.conf
sudo systemctl daemon-reload
sudo systemctl enable --now ethercat
ethercat slaves   # 应看到: 0  0:0  OP  Renesas EtherCAT RA8T2 CiA402 2port
```

> I225 网卡 MAC 获取（勿用 `ip -o link | head -1`，接口顺序与 PCI 顺序不一致）：
> `for i in /sys/class/net/*; do d=$(basename $(readlink $i/device/driver 2>/dev/null) 2>/dev/null); [ "$d" = igc ] && cat $i/address; done`

### 安装应用

```bash
sudo apt install -y ethercat-master   # 含 /usr/bin/ethercat、libethercat、ecrt.h（已由私有 APT 源提供，无需手动编译 IgH）
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

通过 **YAML 配置文件** 管理参数的电机持续转动测试程序，支持 **1~N 个从站**，每个从站独立的运动参数（步进量、往返边界），所有从站启用 DC（Sync0）同步。新增/移除电机只需改配置文件，无需重新编译。

### 运行

```bash
# 必须用 sudo：CSP+DC 模式需要实时调度 (SCHED_FIFO)，否则从站会掉状态
sudo ./build/motor_continuous <duration_ms> [config_path]
```

参数说明：
- `duration_ms > 0`：运行指定毫秒后自动退出（如 `10000` = 10秒）
- `duration_ms = 0`：**无限运行**，只有按 `Ctrl+C` 或收到 `SIGTERM` 才退出（退出前反激活 master）
- `config_path`：可选，配置文件路径，默认 `Config/motor_continuous.yaml`

```bash
sudo ./build/motor_continuous 0                              # 无限运行，默认配置
sudo ./build/motor_continuous 10000                          # 运行 10 秒
sudo ./build/motor_continuous 0 Config/motor_continuous.yaml # 指定配置
```

### 配置文件

`Config/motor_continuous.yaml`：

```yaml
# 全局参数
cycle_ns: 1000000          # 周期 (ns)，1ms = 1000Hz
assign_activate: 0x0301    # DC 激活码 (Sync0)
mode_of_operation: 8        # 0x6060 模式，8 = CSP
print_interval: 100         # 每多少周期打印一次

# 从站列表（1~N 个，每个独立配置）
slaves:
  - name: "motor_0"
    alias: 0
    position: 0
    vendor_id: 0x00000766
    product_code: 0x00000802
    step: 100              # 该从站每周期步进量
    travel_limit: 10000    # 该从站往返边界 (±)
```

- **全局**：`cycle_ns`、`assign_activate`、`mode_of_operation`、`print_interval`
- **每从站**：`name`（打印标识）、`alias`/`position`/`vendor_id`/`product_code`（身份）、`step`/`travel_limit`（运动）
- 新增电机：复制一个 `- name:` 块，改 `name` 和 `position` 即可
- 配置错误（文件不存在/字段缺失/类型不对）会 **fail loud** 退出（退出码 1）

### 为什么必须 sudo

程序启用实时调度（`SCHED_FIFO` 最高优先级 + `mlockall` 锁内存 + 栈预锁定）。CSP+DC 模式对周期精度要求极高，普通用户态进程会被调度抢占导致周期抖动，从站收不到及时周期数据，**watchdog 超时后会从 OP 掉到 SAFEOP / PREOP / INIT**。

### 打印格式说明

每个周期（按 `print_interval`）每个从站各打印一行：
```
[motor_0] [t= 100ms] AL=OP Status=[Rdy On Ena Vol] pos=9976 target=10076 vel=120 tor=15 ↑
```
- `[motor_0]`：从站名称
- `pos`：当前位置（0x6064）
- `target`：目标位置（pos ± step）
- `vel`：实际速度（0x606c）
- `tor`：实际扭矩（0x6077，单位 0.001 Nm）
- `↑/↓`：当前移动方向

状态缩写（启动和结束各打印一次图例）：
- **AL 状态**：`INIT` / `PREOP` / `SAFEOP` / `OP`
- **Status**：`Rdy`(Ready) `On`(Switched On) `Ena`(Enabled) `Flt`(Fault) `Vol`(Voltage) `Dis`(Disabled) `Tgt`(Target Reached) `Ign`(Ignoring Target)

从站到达各自往返边界（±`travel_limit`）方向切换时，打印一条 `[WARN]` 日志。

## 打包成 Debian 包

把 motor_continuous 打成 Debian 包，含 systemd service（**默认 enabled，开机自启**）。

### 打包

```bash
sudo apt install -y debhelper       # 打包工具 (>= 12)
dpkg-buildpackage -us -uc -b         # 生成 ../motor-continuous_1.0.0_arm64.deb
```

> 前置：`ethercat-master` 通过私有 APT 源安装（提供 `ecrt.h` / `libethercat.so`），无需手动编译 IgH。内核模块（`ec_master.ko` 等）需按上文"部署 EtherCAT 内核模块"先就位。

### 安装与 service 管理

```bash
sudo dpkg -i ../motor-continuous_1.0.0_arm64.deb
sudo systemctl status motor-continuous      # 查看状态（默认 enabled + 启动）
sudo systemctl restart motor-continuous     # 改配置后重启
sudo systemctl disable motor-continuous     # 取消开机自启
```

安装后：
- 二进制：`/usr/bin/motor_continuous`
- 配置：`/etc/motor-continuous/config.yaml`（conffile，升级保留改动）
- service：`/lib/systemd/system/motor-continuous.service`（`Requires=ethercat.service`）

service 默认 `duration 0`（无限运行），配置用 `/etc/motor-continuous/config.yaml`。改配置后 `systemctl restart` 生效。
