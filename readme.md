
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