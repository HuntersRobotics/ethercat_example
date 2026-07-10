//
// DC 激活码扫描测试程序
// Copyright (c) 2026 kaylorchen
// SPDX-License-Identifier: TBD
//
// 用法:
//   test_dc_scan <assign_activate> [cycle_ns] [duration_ms]
//   例: test_dc_scan 0x0300 1000000 4000
//
// 对单个 AssignActivate(0x980) 值做一次完整 CSP 实测:
//   - 配置 CSP 模式 + ecrt_slave_config_dc(该值)
//   - 不强制 free_run, 使用设备原始同步模式配合 Sync0
//   - 推进 PDS 状态机到 Operation Enabled
//   - 写入小目标位置, 观察电机是否响应
//   - 判据: Actual Position 是否变化 / Demand Position(0x6062) 是否跟随 / status bit12
//
// 结论供外部 scan 脚本汇总, 找出能让电机转动的正确激活码。
//

#include <ecrt.h>

#include <cstdint>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <iomanip>

#define VENDOR_ID    0x00000766u
#define PRODUCT_CODE 0x00000802u
#define SLAVE_POS    0

#define NSEC_PER_SEC 1000000000L

static bool g_running = true;
static void OnSignal(int) { g_running = false; }

struct PdoOffset {
  unsigned int target_position;
  unsigned int control_word;
  unsigned int position_actual;
  unsigned int status_word;
};

// 安全限位: 单次测试中位置累计行程超过该值则立即停止, 防止失控
static const int32_t kSafetyTravelLimit = 200000;

int main(int argc, char** argv) {
  setvbuf(stdout, nullptr, _IONBF, 0);  // 无缓冲, 防止程序异常退出时丢失输出
  uint16_t assign_activate = 0x0300;
  int64_t  cycle_ns        = 1000000;
  int      duration_ms     = 4000;
  if (argc >= 2) assign_activate = (uint16_t)strtoul(argv[1], nullptr, 0);
  if (argc >= 3) cycle_ns         = (int64_t)strtoul(argv[2], nullptr, 0);
  if (argc >= 4) duration_ms      = atoi(argv[3]);

  signal(SIGINT, OnSignal);
  signal(SIGTERM, OnSignal);

  std::cout << "========== DC AssignActivate 扫描 ==========\n";
  std::cout << "AssignActivate = 0x" << std::hex << std::setw(4)
            << std::setfill('0') << assign_activate << std::dec
            << " (" << assign_activate << ")"
            << ", cycle = " << cycle_ns << " ns"
            << ", duration = " << duration_ms << " ms\n";

  ec_master_t* master = ecrt_request_master(0);
  if (!master) { std::cerr << "[FATAL] request master 失败\n"; return 2; }
  ec_domain_t* domain = ecrt_master_create_domain(master);
  if (!domain) { std::cerr << "[FATAL] create domain 失败\n"; return 2; }
  ec_slave_config_t* sc =
      ecrt_master_slave_config(master, 0, 0, VENDOR_ID, PRODUCT_CODE);
  if (!sc) { std::cerr << "[FATAL] slave config 失败\n"; return 2; }

  PdoOffset off{};
  ec_pdo_entry_reg_t regs[] = {
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x607a, 0x00, &off.target_position},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6040, 0x00, &off.control_word},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6064, 0x00, &off.position_actual},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6041, 0x00, &off.status_word},
      {}};
  if (ecrt_domain_reg_pdo_entry_list(domain, regs)) {
    std::cerr << "[FATAL] register PDO entries 失败\n";
    return 2;
  }

  // CSP 模式 (0x6060 = 8)
  if (ecrt_slave_config_sdo8(sc, 0x6060, 0x00, 8)) {
    std::cerr << "[WARN] 设置 0x6060 (CSP) 失败\n";
  }
  // 注意: 不强制 free_run (不写 0x1C32/0x1C33), 使用设备原始同步模式配合 Sync0

  std::cout << "ecrt_slave_config_dc(0x" << std::hex << assign_activate
            << std::dec << ", " << cycle_ns << " ns, shift=0)\n";
  ecrt_slave_config_dc(sc, assign_activate, (uint32_t)cycle_ns, 0, 0, 0);

  if (ecrt_master_activate(master)) {
    std::cerr << "[FATAL] activate master 失败\n";
    return 2;
  }
  uint8_t* pd = ecrt_domain_data(domain);
  if (!pd) { std::cerr << "[FATAL] get domain data 失败\n"; return 2; }

  // 初始 DC 参考时钟同步
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  uint64_t app_time = (uint64_t)t.tv_sec * NSEC_PER_SEC + t.tv_nsec;
  ecrt_master_application_time(master, app_time);
  ecrt_master_sync_reference_clock(master);
  ecrt_master_sync_slave_clocks(master);

  // 周期循环
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  next.tv_sec += 1;
  next.tv_nsec = 0;

  bool     enabled         = false;
  bool     op_reached      = false;
  bool     fault_seen      = false;
  bool     safety_trip     = false;
  int32_t  initial_pos     = 0;
  int32_t  min_pos         = 0;
  int32_t  max_pos         = 0;
  int32_t  target          = 0;
  uint16_t final_status    = 0;
  uint16_t final_al_state  = 0;
  int      fault_reset_toggle = 0;

  const int total_iters = duration_ms + 1000;  // 多 1s 给状态机初始化
  int iter = 0;
  while (g_running && iter < total_iters) {
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

    ecrt_master_receive(master);
    ecrt_domain_process(domain);

    uint16_t status = EC_READ_U16(pd + off.status_word);
    int32_t  pos    = EC_READ_S32(pd + off.position_actual);
    final_status = status;

    ec_slave_config_state_t s;
    ecrt_slave_config_state(sc, &s);
    final_al_state = s.al_state;
    if (s.al_state == 8) op_reached = true;
    if (status & 0x0008) fault_seen = true;

    if (iter % 500 == 0) {
      std::cout << "[t=" << std::setw(5) << iter << "ms] al=0x"
                << std::hex << (int)s.al_state
                << " status=0x" << std::setw(4) << std::setfill('0')
                << status << std::dec << std::setfill(' ')
                << " pos=" << pos
                << (enabled ? " ENABLED" : "")
                << (fault_seen ? " FAULT" : "") << "\n";
    }

    uint16_t low = status & 0x6f;
    uint16_t ctrl;
    if (low == 0x40) {
      ctrl = 0x06;             // switch on disabled -> shutdown -> ready
    } else if (low == 0x21 || low == 0x23 || low == 0x33) {
      ctrl = 0x0f;             // ready/switched on -> enable operation
    } else if (low == 0x27 || low == 0x37) {
      ctrl = 0x0f;             // operation enabled
      enabled = true;
    } else {
      ctrl = 0x0f;
    }
    if (status & 0x0008) {     // fault: 交替写 fault reset / shutdown
      ctrl = (fault_reset_toggle++ % 2) ? 0x06 : 0x80;
    }

    if (safety_trip) {
      ctrl = 0x06;             // 触发安全停止
    }

    if (!enabled || safety_trip) {
      EC_WRITE_U16(pd + off.control_word, ctrl);
      EC_WRITE_S32(pd + off.target_position, pos);  // 跟随当前位置, 不给目标
    } else {
      if (target == 0) {
        initial_pos = pos;
        target = initial_pos + 1000;   // 小目标, 用户要求初始不要太大
        min_pos = max_pos = pos;
        std::cout << "[INFO] 已使能, initial_pos=" << initial_pos
                  << ", target=" << target << "\n";
      }
      EC_WRITE_U16(pd + off.control_word, 0x0f);
      EC_WRITE_S32(pd + off.target_position, target);
      if (pos > max_pos) max_pos = pos;
      if (pos < min_pos) min_pos = pos;
      // 安全限位
      if ((max_pos - min_pos) > kSafetyTravelLimit) {
        safety_trip = true;
        std::cout << "[WARN] 行程超限, 触发安全停止\n";
      }
    }

    clock_gettime(CLOCK_MONOTONIC, &t);
    app_time = (uint64_t)t.tv_sec * NSEC_PER_SEC + t.tv_nsec;
    ecrt_master_application_time(master, app_time);
    ecrt_master_sync_reference_clock(master);
    ecrt_master_sync_slave_clocks(master);
    ecrt_domain_queue(domain);
    ecrt_master_send(master);

    next.tv_nsec += cycle_ns;
    while (next.tv_nsec >= NSEC_PER_SEC) {
      next.tv_nsec -= NSEC_PER_SEC;
      next.tv_sec++;
    }
    iter++;
  }

  // 注意: Demand Position (0x6062) 不在 TxPDO, 读取需走 SDO upload (阻塞 API),
  // 在从站非 OP 时会长时间阻塞. 为保证扫描程序健壮, 这里不读 0x6062,
  // 改用 PDO 内可直接得到的 Actual Position(0x6064) 变化作为主判据.

  int32_t travel = max_pos - min_pos;
  int     bit12  = (final_status >> 12) & 1;

  std::cout << "\n---------- 结果 ----------\n";
  std::cout << "AssignActivate   : 0x" << std::hex << std::setw(4)
            << std::setfill('0') << assign_activate << std::dec << "\n";
  std::cout << "AL state (末值)  : 0x" << std::hex << (int)final_al_state
            << std::dec << " (OP=" << (op_reached ? "YES" : "NO") << ")\n";
  std::cout << "Operation enabled: " << (enabled ? "YES" : "NO") << "\n";
  std::cout << "Fault 发生过     : " << (fault_seen ? "YES" : "NO") << "\n";
  std::cout << "安全停止触发     : " << (safety_trip ? "YES" : "NO") << "\n";
  std::cout << "status word (末值): 0x" << std::hex << std::setw(4)
            << std::setfill('0') << final_status << std::dec << "\n";
  std::cout << "bit12 (CSP目标忽略): " << bit12 << "\n";
  std::cout << "Target Position  : " << target << "\n";
  std::cout << "Initial Position : " << initial_pos << "\n";
  std::cout << "Actual Position  : [" << min_pos << " ~ " << max_pos
            << "], travel = " << travel << "\n";

  std::cout << "\n>>> 结论: ";
  bool motor_moved = enabled && (travel > 10);
  if (motor_moved) {
    std::cout << "MOTOR_MOVED (travel=" << travel << ") — 该激活码可能正确!\n";
  } else if (op_reached && enabled && bit12 == 0) {
    std::cout << "BIT12_CLEAR (进OP使能且 bit12=0 目标未被忽略) — Sync0 可能生效\n";
  } else if (op_reached && enabled) {
    std::cout << "OP_BUT_IDLE (进OP使能, 但 bit12=" << bit12
              << " 目标被忽略, travel=" << travel << ") — Sync0 未生效\n";
  } else {
    std::cout << "NOT_WORKING (enabled=" << (enabled ? 1 : 0)
              << ", OP=" << (op_reached ? 1 : 0)
              << ", fault=" << (fault_seen ? 1 : 0) << ")\n";
  }

  ecrt_master_deactivate(master);
  ecrt_release_master(master);
  return 0;
}
