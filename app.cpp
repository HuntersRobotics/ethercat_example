//
// Created by kaylor on 5/14/25.
//

#include "app.h"
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

const static ec_pdo_entry_reg_t pdo_entry_regs[] = {
    {0,1, 0x5a65726f, 0x00029252 , 0x607a, 0x00, &user_data.target_position},
    {0,1,  0x5a65726f, 0x00029252, 0x6040, 0x00, &user_data.control_word},
    {0,1,  0x5a65726f, 0x00029252, 0x6064, 0x00, &user_data.position_value},
    {0,1,  0x5a65726f, 0x00029252, 0x6041, 0x00, &user_data.status_word},
    {0,0,  0x300077, 0x1, 0x7000, 0x01, &user_data.output_bit1, &user_data.offset_output_bit1},
    {0,0,  0x300077, 0x1, 0x7000, 0x02, &user_data.output_bit2, &user_data.offset_output_bit2},
    {0,0,  0x300077, 0x1, 0x6000, 0x01, &user_data.input_bit1, &user_data.offset_input_bit1},
    {0,0,  0x300077, 0x1, 0x6000, 0x02, &user_data.input_bit2, &user_data.offset_input_bit2},
    {}};

void App::Config() {
  std::vector<EthercatNode::SalveParam> salve_param;
  EthercatNode::SalveParam zeroerr_motor;
  zeroerr_motor.alias = 0;
  zeroerr_motor.position = 1;
  zeroerr_motor.vendor_id = 0x5a65726f;
  zeroerr_motor.product_code = 0x29252;
  salve_param.push_back(zeroerr_motor);
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
  uint8_t input1 = EC_READ_BIT(process_data_ + user_data.input_bit1, user_data.offset_input_bit1);
  uint8_t input2 = EC_READ_BIT(process_data_ + user_data.input_bit2, user_data.offset_input_bit2);
  EC_WRITE_BIT(process_data_ + user_data.output_bit1, user_data.offset_output_bit1, input1);
  EC_WRITE_BIT(process_data_ + user_data.output_bit2, user_data.offset_output_bit2, input2);
  static uint32_t num = 0;
  if (num == 999) {
    num = 0;
    KAYLORDUT_LOG_INFO("status: 0x{:04X}, position value: {}, input1: {}, input2: {}", status,
                       position, input1, input2);
  }
  num++;
  // send process data
  ecrt_domain_queue(domain_);
  ecrt_master_send(master_);
}


