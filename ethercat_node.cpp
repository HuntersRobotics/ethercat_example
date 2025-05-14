//
// Created by kaylor on 4/30/25.
//

#include "ethercat_node.h"

#include "kaylordut/log/logger.h"

EthercatNode::EthercatNode() {}

EthercatNode::~EthercatNode() {}

void EthercatNode::Initialize(const std::vector<SalveParam> salve_param,
                              const ec_pdo_entry_reg_t* regs,
                              unsigned int master_index) {
  master_ = ecrt_request_master(0);
  if (!master_) {
    KAYLORDUT_LOG_ERROR("Failed to request master of EthercatNode");
    exit(EXIT_FAILURE);
  }
  domain_ = ecrt_master_create_domain(master_);
  if (!domain_) {
    KAYLORDUT_LOG_ERROR("Failed to create domain of EthercatNode");
    exit(EXIT_FAILURE);
  }
  for (const auto& param : salve_param) {
    EcSlaveConfig config;
    if (!(config.sc =
              ecrt_master_slave_config(master_, param.alias, param.position,
                                       param.vendor_id, param.product_code))) {
      KAYLORDUT_LOG_ERROR(
          "Failed to get a slave configuration, alias: {}, position: {}, "
          "vendor_id: 0x{:X}, product_code: 0x{:X}",
          param.alias, param.position, param.vendor_id, param.product_code);
      exit(EXIT_FAILURE);
    }
    config.alias = param.alias;
    config.position = param.position;
    config.vendor_id = param.vendor_id;
    config.product_code = param.product_code;
    slave_configs_.push_back(config);
  }
  pdo_entry_regs_ = regs;
  if (ecrt_domain_reg_pdo_entry_list(domain_, pdo_entry_regs_)) {
    KAYLORDUT_LOG_ERROR("Failed to register pdo entry list");
    exit(EXIT_FAILURE);
  }
  KAYLORDUT_LOG_INFO("Activating master...");
  if (ecrt_master_activate(master_)) {
    KAYLORDUT_LOG_ERROR("Failed to activate master");
    exit(EXIT_FAILURE);
  }
  if (!(process_data_ = ecrt_domain_data(domain_))) {
    KAYLORDUT_LOG_ERROR("Failed to get domain data");
    exit(EXIT_FAILURE);
  }
  KAYLORDUT_LOG_INFO("EthercatNode initialized");
}

void EthercatNode::CheckMasterState() {
  ec_master_state_t ms;
  ecrt_master_state(master_, &ms);
  if (ms.slaves_responding != master_state_.slaves_responding) {
    KAYLORDUT_LOG_INFO("{} slave(s).", ms.slaves_responding);
  }
  if (ms.al_states != master_state_.al_states) {
    KAYLORDUT_LOG_INFO("AL states: 0x{:02X}.",
                       static_cast<unsigned int>(ms.al_states));
  }
  if (ms.link_up != master_state_.link_up) {
    KAYLORDUT_LOG_INFO("Link is {}.",
                       static_cast<unsigned int>(ms.link_up) ? "up" : "down");
  }
  master_state_ = ms;
}

void EthercatNode::CheckSalveConfigStates() {
  ec_slave_config_state_t s;
  for (auto& slave_config : slave_configs_) {
    ecrt_slave_config_state(slave_config.sc, &s);
    if (s.al_state != slave_config.sc_state.al_state) {
      KAYLORDUT_LOG_INFO("alias: {}, position: {} State 0x{:02X}.",
                         slave_config.alias, slave_config.position,
                         static_cast<unsigned int>(s.al_state));
    }
    if (s.online != slave_config.sc_state.online) {
      KAYLORDUT_LOG_INFO("alias: {}, position: {} ---> {}", slave_config.alias,
                         slave_config.position,
                         s.online ? "online" : "offline");
    }
    if (s.operational != slave_config.sc_state.operational) {
      KAYLORDUT_LOG_INFO("alias: {}, position: {} ---> {}operational.\n",
                         slave_config.alias, slave_config.position,
                         s.operational ? "" : "Not ");
    }
    slave_config.sc_state = s;
  }
}

void EthercatNode::CheckoutDomainState() {
  ec_domain_state_t ds;
  ecrt_domain_state(domain_, &ds);
  if (ds.working_counter != domain_state_.working_counter) {
    KAYLORDUT_LOG_INFO("Domain: WC {}.", ds.working_counter);
  }
  if (ds.wc_state != domain_state_.wc_state) {
    KAYLORDUT_LOG_INFO("Domain: State {}.", ds.wc_state);
  }
  domain_state_ = ds;
}



