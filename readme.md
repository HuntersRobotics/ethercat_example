# AI Hunter Ethercat Demo
## Install Driver and Application
### Add My Private APT Repository  
```bash
cat << 'EOF' | sudo tee /etc/apt/sources.list.d/kaylordut.list 
deb [arch=arm64 signed-by=/etc/apt/keyrings/kaylor-keyring.gpg] http://apt.kaylordut.cn/kaylordut/ kaylordut main
EOF
sudo mkdir /etc/apt/keyrings -pv
sudo wget -O /etc/apt/keyrings/kaylor-keyring.gpg http://apt.kaylordut.cn/kaylor-keyring.gpg
sudo apt update
```
### Check Kernel Version and Install Driver
```bash
╰─ uname -a                                                                                                                                ─╯
Linux ai-hunter 6.1.43-rt14-rockchip-rk3588 #1.0.0 SMP PREEMPT_RT Fri Jul 25 16:37:54 CST 2025 aarch64 aarch64 aarch64 GNU/Linux

╰─ apt policy ethercat-module                                                                                                              ─╯
ethercat-module:
  Installed: (none)
  Candidate: 6.1.43-rt14-rockchip-rk3588
  Version table:
     6.1.43-rt14-rockchip-rk3588 500
        500 http://apt.kaylordut.cn/kaylordut kaylordut/main arm64 Packages
     5.10.160-rt89-rockchip-rk3588 500
        500 http://apt.kaylordut.cn/kaylordut kaylordut/main arm64 Packages
```
```bash
# !!!!!
sudo apt install -y ethercat-module=6.1.43-rt14-rockchip-rk3588 # if kernel version is 6.1.43-rt14-rockchip-rk3588
# !!!!!
sudo apt install -y ethercat-module=5.10.160-rt89-rockchip-rk3588 # if kernel vesion is 5.10.160-rt89-rockchip-rk3588
```
> Pls notice kernel version 

### Install Application
```bash
sudo apt install -y ethercat-master
```

## Ethercat状态信息

```bash
╰─ ethercat cstruct                                                                                                                        ─╯
/* Master 0, Slave 0, "ioSample"
 * Vendor ID:       0x00300077
 * Product code:    0x00000001
 * Revision number: 0x00000001
 */

ec_pdo_entry_info_t slave_0_pdo_entries[] = {
    {0x7000, 0x01, 1}, /* OUTPUT_BIT1 */
    {0x7000, 0x02, 1}, /* OUTPUT_BIT2 */
    {0x0000, 0x00, 6}, /* Gap */
    {0x6000, 0x01, 1}, /* ININPUT_BIT1 */
    {0x6000, 0x02, 1}, /* ININPUT_BIT2 */
    {0x0000, 0x00, 6}, /* Gap */
};

ec_pdo_info_t slave_0_pdos[] = {
    {0x1600, 3, slave_0_pdo_entries + 0}, /* OUTPUT process data mapping */
    {0x1a00, 3, slave_0_pdo_entries + 3}, /* INPUT process data mapping */
};

ec_sync_info_t slave_0_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, slave_0_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT, 1, slave_0_pdos + 1, EC_WD_DISABLE},
    {0xff}
};

/* Master 0, Slave 1, "ZeroErr Driver"
 * Vendor ID:       0x5a65726f
 * Product code:    0x00029252
 * Revision number: 0x00000001
 */

ec_pdo_entry_info_t slave_1_pdo_entries[] = {
    {0x607a, 0x00, 32}, /* Target Position */
    {0x60fe, 0x00, 32}, /* Digital outputs */
    {0x6040, 0x00, 16}, /* Control Word */
    {0x6064, 0x00, 32}, /* Position Actual Value */
    {0x60fd, 0x00, 32}, /* Digital inputs */
    {0x6041, 0x00, 16}, /* Status Word */
};

ec_pdo_info_t slave_1_pdos[] = {
    {0x1600, 3, slave_1_pdo_entries + 0}, /* R0PDO */
    {0x1a00, 3, slave_1_pdo_entries + 3}, /* T0PDO */
};

ec_sync_info_t slave_1_syncs[] = {
    {0, EC_DIR_OUTPUT, 0, NULL, EC_WD_DISABLE},
    {1, EC_DIR_INPUT, 0, NULL, EC_WD_DISABLE},
    {2, EC_DIR_OUTPUT, 1, slave_1_pdos + 0, EC_WD_ENABLE},
    {3, EC_DIR_INPUT, 1, slave_1_pdos + 1, EC_WD_DISABLE},
    {0xff}
};


╰─ ethercat slaves                                                                                                                         ─╯
0  0:0  PREOP  +  ioSample
1  0:1  PREOP  +  ZeroErr Driver

```
> 从上面打印信息看到 MCU的 别名是0， 位置是0， ZeroErr的 别名是0 ， 位置是1
> 同时注意PDO的映射信息