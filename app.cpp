//
// Created by kaylor on 5/14/25.
//

#include "app.h"

#include <thread>

#include "kaylordut/log/logger.h"

struct PDO_offset {
  unsigned int target_position;
  unsigned int control_word;
  unsigned int position_value;
  unsigned int status_word;
  unsigned int output_bit1;
  unsigned int output_bit2;
  unsigned int input_bit1;
  unsigned int input_bit2;
  unsigned int offset_output_bit1;
  unsigned int offset_output_bit2;
  unsigned int offset_input_bit1;
  unsigned int offset_input_bit2;
} user_data;

#define ZERO_ERR_ALIAS_POSITION 0,1
#define ZERO_ERR_VENDORID_PRODUCTCODE 0x5a65726f, 0x00029252
#define MCU_ALIAS_POSITION 0,0
#define MCU_VENDORID_PRODUCTCODE 0x300077, 0x1

const static ec_pdo_entry_reg_t pdo_entry_regs[] = {
    {ZERO_ERR_ALIAS_POSITION, ZERO_ERR_VENDORID_PRODUCTCODE, 0x607a, 0x00, &user_data.target_position},
    {ZERO_ERR_ALIAS_POSITION, ZERO_ERR_VENDORID_PRODUCTCODE, 0x6040, 0x00, &user_data.control_word},
    {ZERO_ERR_ALIAS_POSITION, ZERO_ERR_VENDORID_PRODUCTCODE, 0x6064, 0x00, &user_data.position_value},
    {ZERO_ERR_ALIAS_POSITION, ZERO_ERR_VENDORID_PRODUCTCODE, 0x6041, 0x00, &user_data.status_word},
    {MCU_ALIAS_POSITION,  MCU_VENDORID_PRODUCTCODE, 0x7000, 0x01, &user_data.output_bit1, &user_data.offset_output_bit1},
    {MCU_ALIAS_POSITION,  MCU_VENDORID_PRODUCTCODE, 0x7000, 0x02, &user_data.output_bit2, &user_data.offset_output_bit2},
    {MCU_ALIAS_POSITION,  MCU_VENDORID_PRODUCTCODE, 0x6000, 0x01, &user_data.input_bit1, &user_data.offset_input_bit1},
    {MCU_ALIAS_POSITION,  MCU_VENDORID_PRODUCTCODE, 0x6000, 0x02, &user_data.input_bit2, &user_data.offset_input_bit2},
    {}};

App::App(bool* running) { running_ = running; }

App::~App() {}

void App::Config() {
  std::vector<EthercatNode::SalveParam> salve_param;
  EthercatNode::SalveParam sample;
  // 这里从readme中的信息获取一下从站的各种信息并配置从站
  sample.alias = 0;
  sample.position = 1;
  sample.vendor_id = 0x5a65726f;
  sample.product_code = 0x29252;
  salve_param.push_back(sample);
  sample.alias = 0;
  sample.position = 0;
  sample.vendor_id = 0x300077;
  sample.product_code = 0x01;
  salve_param.push_back(sample);
  // 这里只有一个master，所以index是0
  Initialize(salve_param, pdo_entry_regs, 0);
}

void App::RunOnce() {
  ecrt_master_receive(master_);
  ecrt_domain_process(domain_);
  CheckoutDomainState();
  // read process data
  auto status = EC_READ_U16(process_data_ + user_data.status_word);
  auto position = EC_READ_U32(process_data_ + user_data.position_value);
  // 读取国民技术的IO的状态，同时把IO状态写到LED上去
  uint8_t input1 = EC_READ_BIT(process_data_ + user_data.input_bit1,
                               user_data.offset_input_bit1);
  uint8_t input2 = EC_READ_BIT(process_data_ + user_data.input_bit2,
                               user_data.offset_input_bit2);
  EC_WRITE_BIT(process_data_ + user_data.output_bit1,
               user_data.offset_output_bit1, input1);
  EC_WRITE_BIT(process_data_ + user_data.output_bit2,
               user_data.offset_output_bit2, input2);
  if (slave_configs_.at(0).sc_state.operational == 1 &&
      (status & 0x0F) == 0x07) {
    // 只有准备打开伺服 伺服使能
    // 和伺服运行同时满足，并且没有故障的时候，才下发控制指令
    if ((status & 0x0400)) {
      // 如果到达目标位置了，才会切换下一个目标
      KAYLORDUT_LOG_DEBUG("set target position to {}", position - 100);
      EC_WRITE_S32(process_data_ + user_data.target_position, position - 100);
    }
  }

  static uint32_t num = 0;
  if (num == 999) {
    num = 0;
    KAYLORDUT_LOG_INFO(
        "status: 0x{:04X}, position value: {}, input1: {}, input2: {}", status,
        position, input1, input2);
  }
  num++;
  // send process data
  ecrt_domain_queue(domain_);
  ecrt_master_send(master_);
}

bool App::InitializeDevices() {
  bool ret = false;
  while (*running_ && (!ret)) {
    ecrt_master_receive(master_);
    ecrt_domain_process(domain_);
    CheckoutDomainState();
    CheckSalveConfigStates();
    auto status = EC_READ_U16(process_data_ + user_data.status_word);
    auto position = EC_READ_U32(process_data_ + user_data.position_value);
    /// 下面是配置电机周期位置模式初始化的流程
    /// 1. 读取了当前的位置，写到目标位置（这是因为如果目标位置和初始位置相差太远的话，电机会
    /// 直接报错）
    /// 2. 清除电机所有的错误
    /// 3. 使能电机 依次发送 0x06 0x07 0x0F
    if (slave_configs_.at(0).sc_state.operational == 1) {
      static bool first_boot = true;
      if (first_boot) {
        first_boot = false;
        KAYLORDUT_LOG_INFO("write current position({}) to target position",
                           position);
        EC_WRITE_S32(process_data_ + user_data.target_position, position);
      }
      if (status & 0x0008) {
        KAYLORDUT_LOG_INFO("current status: 0x{:04X}, control word --> 0x80",
                           status);
        // 清除电机报警
        EC_WRITE_U16(process_data_ + user_data.control_word, 0x80);
      } else {
        if (status & 0x10) {
          if ((status & 0x50) == 0x50) {
            KAYLORDUT_LOG_INFO(
                "current status: 0x{:04X}, control word --> 0x06", status);
            EC_WRITE_U16(process_data_ + user_data.control_word, 0x06);
          } else if ((status & 0x33) == 0x33) {
            if ((status & 0x0F) == 0x07) {
              static int count = 0;
              count++;
              KAYLORDUT_LOG_INFO("count = {}", count);
              if (count == 100) {
                KAYLORDUT_LOG_INFO("Initialization completed.");
                ret = true;
              }
            } else {
              KAYLORDUT_LOG_INFO(
                  "current status: 0x{:04X}, control word --> 0x0F", status);
              EC_WRITE_U16(process_data_ + user_data.control_word, 0x0F);
            }
          } else if ((status & 0x33) == 0x31) {
            KAYLORDUT_LOG_INFO(
                "current status: 0x{:04X}, control word --> 0x07", status);
            EC_WRITE_U16(process_data_ + user_data.control_word, 0x07);
          }
        }
      }
    }
    // send process data
    ecrt_domain_queue(domain_);
    ecrt_master_send(master_);
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    KAYLORDUT_LOG_DEBUG("InitializeDevices() is Running");
  }
  return ret;
}
