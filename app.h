//
// Created by kaylor on 5/14/25.
//

#ifndef APP_H
#define APP_H

#include "ethercat_node.h"

class App :public EthercatNode{
public:
 App(bool *running);
 ~App();
 void Config();
 bool InitializeDevices();
 virtual void RunOnce() override;

private:
 bool *running_;
};



#endif //APP_H
