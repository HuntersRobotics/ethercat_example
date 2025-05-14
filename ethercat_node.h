//
// Created by kaylor on 4/30/25.
//

#ifndef ETHERCAT_NODE_H
#define ETHERCAT_NODE_H

#include <vector>

#include "ecrt.h"
#include "ethercat_node.h"

class EthercatNode {
 public:
  typedef struct {
    uint16_t alias;
    uint16_t position;
    uint32_t vendor_id;
    uint32_t product_code;
  } SalveParam;
  EthercatNode();
  ~EthercatNode();
  virtual void RunOnce() = 0;
  void CheckMasterState();
  void CheckSalveConfigStates();
  void CheckoutDomainState();

 protected:
  void Initialize(const std::vector<SalveParam> salve_param, const ec_pdo_entry_reg_t *regs, unsigned int master_index = 0);
  ec_master_t *master_ = nullptr;
  ec_domain_t *domain_ = nullptr;
  uint8_t *process_data_{nullptr};

 private:
  ec_master_state_t master_state_;
  ec_domain_state_t domain_state_;
  typedef struct {
    uint16_t alias;
    uint16_t position;
    uint32_t vendor_id;
    uint32_t product_code;
    ec_slave_config_t *sc;
    ec_slave_config_state_t sc_state;
  } EcSlaveConfig;
  std::vector<EcSlaveConfig> slave_configs_;
  const ec_pdo_entry_reg_t *pdo_entry_regs_;
};



#endif //ETHERCAT_NODE_H
