//
// Created by kaylor on 5/14/25.
//

#include "app.h"

#include <thread>

#include "kaylordut/log/logger.h"

struct PDO_offset {
  unsigned int led1;
  unsigned int led2;
  unsigned int led3;
  unsigned int led4;
  unsigned int led5;
  unsigned int led6;
  unsigned int led7;
  unsigned int led8;
  unsigned int led1_bit_position;
  unsigned int led2_bit_position;
  unsigned int led3_bit_position;
  unsigned int led4_bit_position;
  unsigned int led5_bit_position;
  unsigned int led6_bit_position;
  unsigned int led7_bit_position;
  unsigned int led8_bit_position;
  unsigned int switch1;
  unsigned int switch2;
  unsigned int switch3;
  unsigned int switch4;
  unsigned int switch5;
  unsigned int switch6;
  unsigned int switch7;
  unsigned int switch8;
  unsigned int switch1_bit_position;
  unsigned int switch2_bit_position;
  unsigned int switch3_bit_position;
  unsigned int switch4_bit_position;
  unsigned int switch5_bit_position;
  unsigned int switch6_bit_position;
  unsigned int switch7_bit_position;
  unsigned int switch8_bit_position;
  unsigned int underrange;
  unsigned int underrange_bit_position;
  unsigned int overrange;
  unsigned int overrange_bit_position;
  unsigned int limit1;
  unsigned int limit1_bit_position;
  unsigned int limit2;
  unsigned int limit2_bit_position;
  unsigned int tx_pdo_state;
  unsigned int tx_pdo_state_bit_position;
  unsigned int tx_pdo_toggle;
  unsigned int tx_pdo_toggle_bit_position;
  unsigned int analog_input;
  unsigned int analog_input_bit_position;
} user_data;

#define LAN9252_EVB_ALIAS_POSITION 0, 0
#define LAN9252_EVB_VENDORID_PRODUCTCODE 0x09, 0x9252

const static ec_pdo_entry_reg_t pdo_entry_regs[] = {
    // RxPDO 0x1601 - LED outputs
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x01, &user_data.led1, &user_data.led1_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x02, &user_data.led2, &user_data.led2_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x03, &user_data.led3, &user_data.led3_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x04, &user_data.led4, &user_data.led4_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x05, &user_data.led5, &user_data.led5_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x06, &user_data.led6, &user_data.led6_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x07, &user_data.led7, &user_data.led7_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x7010, 0x08, &user_data.led8, &user_data.led8_bit_position},

    // TxPDO 0x1a00 - Switch inputs
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x01, &user_data.switch1, &user_data.switch1_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x02, &user_data.switch2, &user_data.switch2_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x03, &user_data.switch3, &user_data.switch3_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x04, &user_data.switch4, &user_data.switch4_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x05, &user_data.switch5, &user_data.switch5_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x06, &user_data.switch6, &user_data.switch6_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x07, &user_data.switch7, &user_data.switch7_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6000, 0x08, &user_data.switch8, &user_data.switch8_bit_position},
    // TxPDO 0x1a02 - Analog inputs
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6020, 0x01, &user_data.underrange, &user_data.underrange_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6020, 0x02, &user_data.overrange, &user_data.overrange_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6020, 0x03, &user_data.limit1, &user_data.limit1_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6020, 0x05, &user_data.limit2, &user_data.limit2_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x1802, 0x07, &user_data.tx_pdo_state, &user_data.tx_pdo_state_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x1802, 0x09, &user_data.tx_pdo_toggle, &user_data.tx_pdo_toggle_bit_position},
    {LAN9252_EVB_ALIAS_POSITION, LAN9252_EVB_VENDORID_PRODUCTCODE, 0x6020, 0x11, &user_data.analog_input, &user_data.analog_input_bit_position},
    {}
};

App::App(bool* running) { running_ = running; }

App::~App() {}

void App::Config() {
  std::vector<EthercatNode::SalveParam> salve_param;
  EthercatNode::SalveParam sample;
  // 这里从readme中的信息获取一下从站的各种信息并配置从站
  sample.alias = 0;
  sample.position = 0;
  sample.vendor_id = 0x09;
  sample.product_code = 0x9252;
  salve_param.push_back(sample);
  // sample.alias = 0;
  // sample.position = 0;
  // sample.vendor_id = 0x300077;
  // sample.product_code = 0x01;
  // salve_param.push_back(sample);
  // 这里只有一个master，所以index是0
  Initialize(salve_param, pdo_entry_regs, 0);
}

void App::RunOnce() {
  ecrt_master_receive(master_);
  ecrt_domain_process(domain_);
  CheckoutDomainState();
  // read process data
  uint8_t switch_state[8];
  switch_state[0] =
      EC_READ_BIT(process_data_ + user_data.switch1, user_data.switch1_bit_position);
  switch_state[1] =
      EC_READ_BIT(process_data_ + user_data.switch2, user_data.switch2_bit_position);
  switch_state[2] =
      EC_READ_BIT(process_data_ + user_data.switch3, user_data.switch3_bit_position);
  switch_state[3] =
      EC_READ_BIT(process_data_ + user_data.switch4, user_data.switch4_bit_position);
  switch_state[4] =
      EC_READ_BIT(process_data_ + user_data.switch5, user_data.switch5_bit_position);
  switch_state[5] =
      EC_READ_BIT(process_data_ + user_data.switch6, user_data.switch6_bit_position);
  switch_state[6] =
      EC_READ_BIT(process_data_ + user_data.switch7, user_data.switch7_bit_position);
  switch_state[7] =
      EC_READ_BIT(process_data_ + user_data.switch8, user_data.switch8_bit_position);
  uint16_t adc = EC_READ_U16(process_data_ + user_data.analog_input);
  EC_WRITE_BIT(process_data_ + user_data.led1, user_data.led1_bit_position, switch_state[0]);
  EC_WRITE_BIT(process_data_ + user_data.led2, user_data.led2_bit_position, switch_state[1]);
  EC_WRITE_BIT(process_data_ + user_data.led3, user_data.led3_bit_position, switch_state[2]);
  EC_WRITE_BIT(process_data_ + user_data.led4, user_data.led4_bit_position, switch_state[3]);
  EC_WRITE_BIT(process_data_ + user_data.led5, user_data.led5_bit_position, switch_state[4]);
  EC_WRITE_BIT(process_data_ + user_data.led6, user_data.led6_bit_position, switch_state[5]);
  EC_WRITE_BIT(process_data_ + user_data.led7, user_data.led7_bit_position, switch_state[6]);
  EC_WRITE_BIT(process_data_ + user_data.led8, user_data.led8_bit_position, switch_state[7]);
  KAYLORDUT_LOG_INFO(
      "Switch States: {} {} {} {} {} {} {} {}, adc: {:05d}",
      static_cast<int>(switch_state[0]), static_cast<int>(switch_state[1]),
      static_cast<int>(switch_state[2]), static_cast<int>(switch_state[3]),
      static_cast<int>(switch_state[4]), static_cast<int>(switch_state[5]),
      static_cast<int>(switch_state[6]), static_cast<int>(switch_state[7]),
      static_cast<int>(adc))

  // num++;
  // send process data
  ecrt_domain_queue(domain_);
  ecrt_master_send(master_);
}

bool App::InitializeDevices() {
  bool ret = false;
  // while (*running_ && (!ret)) {
  //   ecrt_master_receive(master_);
  //   ecrt_domain_process(domain_);
  //   CheckoutDomainState();
  //   CheckSalveConfigStates();
  //   auto status = EC_READ_U16(process_data_ + user_data.status_word);
  //   auto position = EC_READ_U32(process_data_ + user_data.position_value);
  //   /// 下面是配置电机周期位置模式初始化的流程
  //   /// 1.
  //   /// 读取了当前的位置，写到目标位置（这是因为如果目标位置和初始位置相差太远的话，电机会
  //   /// 直接报错）
  //   /// 2. 清除电机所有的错误
  //   /// 3. 使能电机 依次发送 0x06 0x07 0x0F
  //   if (slave_configs_.at(0).sc_state.operational == 1) {
  //     static bool first_boot = true;
  //     if (first_boot) {
  //       first_boot = false;
  //       KAYLORDUT_LOG_INFO("write current position({}) to target position",
  //                          position);
  //       EC_WRITE_S32(process_data_ + user_data.target_position, position);
  //     }
  //     if (status & 0x0008) {
  //       KAYLORDUT_LOG_INFO("current status: 0x{:04X}, control word --> 0x80",
  //                          status);
  //       // 清除电机报警
  //       EC_WRITE_U16(process_data_ + user_data.control_word, 0x80);
  //     } else {
  //       if (status & 0x10) {
  //         if ((status & 0x50) == 0x50) {
  //           KAYLORDUT_LOG_INFO(
  //               "current status: 0x{:04X}, control word --> 0x06", status);
  //           EC_WRITE_U16(process_data_ + user_data.control_word, 0x06);
  //         } else if ((status & 0x33) == 0x33) {
  //           if ((status & 0x0F) == 0x07) {
  //             static int count = 0;
  //             count++;
  //             KAYLORDUT_LOG_INFO("count = {}", count);
  //             if (count == 100) {
  //               KAYLORDUT_LOG_INFO("Initialization completed.");
  //               ret = true;
  //             }
  //           } else {
  //             KAYLORDUT_LOG_INFO(
  //                 "current status: 0x{:04X}, control word --> 0x0F", status);
  //             EC_WRITE_U16(process_data_ + user_data.control_word, 0x0F);
  //           }
  //         } else if ((status & 0x33) == 0x31) {
  //           KAYLORDUT_LOG_INFO(
  //               "current status: 0x{:04X}, control word --> 0x07", status);
  //           EC_WRITE_U16(process_data_ + user_data.control_word, 0x07);
  //         }
  //       }
  //     }
  //   }
  //   // send process data
  //   ecrt_domain_queue(domain_);
  //   ecrt_master_send(master_);
  //   std::this_thread::sleep_for(std::chrono::milliseconds(10));
  //   KAYLORDUT_LOG_DEBUG("InitializeDevices() is Running");
  // }
  return ret;
}
