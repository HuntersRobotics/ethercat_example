//
// Created by kaylor on 5/14/25.
//

#ifndef APP_H
#define APP_H

#include "ethercat_node.h"

class App :public EthercatNode{
public:
  void Config();
  virtual void RunOnce() override;
};



#endif //APP_H
