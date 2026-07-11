//
// motor_continuous - Renesas RA8T2 CiA402 电机持续转动测试程序（多从站 + YAML 配置）
// Copyright (c) 2026 kaylorchen
// SPDX-License-Identifier: TBD
//
// 用法:
//   motor_continuous <duration_ms> [config_path]
//   例: motor_continuous 0                          (无限运行，Ctrl+C 退出)
//       motor_continuous 10000                       (运行 10 秒)
//       motor_continuous 0 Config/motor_continuous.yaml
//
// 功能:
//   - 通过 YAML 配置文件管理参数，支持 1~N 个从站
//   - 每个从站独立的运动参数 (step / travel_limit)
//   - DC 激活码、CSP 模式、周期均可配置
//   - 目标策略: 每个从站在 ±travel_limit 边界之间往返
//   - 实时调度 SCHED_FIFO (需 sudo)
//

#include <ecrt.h>
#include <yaml-cpp/yaml.h>

#include <sched.h>
#include <sys/mman.h>

#include <cstdint>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <fstream>
#include <string>
#include <vector>

#include "kaylordut/log/logger.h"

#define NSEC_PER_SEC 1000000000L

static bool g_running = true;
static void OnSignal(int) { g_running = false; }

// 实时调度相关：预先触发栈页错误，避免运行时缺页延迟
#define MAX_SAFE_STACK (8 * 1024)
static void StackPrefault() {
  unsigned char dummy[MAX_SAFE_STACK];
  memset(dummy, 0, MAX_SAFE_STACK);
}

// ===== 配置结构（从 YAML 读取）=====
struct GlobalConfig {
  int32_t cycle_ns;          // 周期 (ns)
  int32_t assign_activate;   // DC 激活码
  int32_t mode_of_operation; // 0x6060 模式 (8=CSP)
  int32_t print_interval;    // 每多少周期打印一次
};

struct SlaveConfig {
  std::string name;
  uint16_t alias = 0;
  uint16_t position = 0;
  uint32_t vendor_id = 0;
  uint32_t product_code = 0;
  int32_t step = 100;            // 每周期步进量
  int32_t travel_limit = 10000;  // 往返边界 (±)
};

// ===== 从站运行时状态 =====
struct SlaveRuntime {
  SlaveConfig cfg;
  ec_slave_config_t* sc = nullptr;
  // PDO 偏移（每从站一组）
  uint32_t off_target_position = 0;
  uint32_t off_control_word = 0;
  uint32_t off_position_actual = 0;
  uint32_t off_status_word = 0;
  uint32_t off_velocity_actual = 0;
  uint32_t off_torque_actual = 0;
  // 运行时状态
  bool enabled = false;
  bool initial_pos_recorded = false;
  int32_t direction = 1;
  int32_t last_direction = 1;
  int32_t fault_reset_toggle = 0;
  int32_t initial_pos = 0;
  int32_t min_pos = 0;
  int32_t max_pos = 0;
  uint16_t final_status = 0;
  uint8_t final_al_state = 0;
};

// 读取 YAML 配置。失败返回 false (fail loud)。
bool LoadConfig(const std::string& path, GlobalConfig& g,
                std::vector<SlaveConfig>& slaves) {
  YAML::Node root;
  try {
    root = YAML::LoadFile(path);
  } catch (const std::exception& e) {
    KAYLORDUT_LOG_ERROR("无法读取配置文件 {}: {}", path, e.what());
    return false;
  }
  try {
    g.cycle_ns = root["cycle_ns"].as<int32_t>();
    g.assign_activate = root["assign_activate"].as<int32_t>();
    g.mode_of_operation = root["mode_of_operation"].as<int32_t>();
    g.print_interval = root["print_interval"].as<int32_t>();
    for (const auto& s : root["slaves"]) {
      SlaveConfig sc;
      sc.name = s["name"].as<std::string>();
      sc.alias = s["alias"].as<uint16_t>();
      sc.position = s["position"].as<uint16_t>();
      sc.vendor_id = s["vendor_id"].as<uint32_t>();
      sc.product_code = s["product_code"].as<uint32_t>();
      sc.step = s["step"].as<int32_t>();
      sc.travel_limit = s["travel_limit"].as<int32_t>();
      slaves.push_back(sc);
    }
  } catch (const std::exception& e) {
    KAYLORDUT_LOG_ERROR("解析配置失败 (检查字段是否完整/类型正确): {}", e.what());
    return false;
  }
  if (slaves.empty()) {
    KAYLORDUT_LOG_ERROR("配置中没有从站 (slaves 列表为空)");
    return false;
  }
  if (g.cycle_ns <= 0 || g.print_interval <= 0) {
    KAYLORDUT_LOG_ERROR("cycle_ns 和 print_interval 必须为正数");
    return false;
  }
  return true;
}

// AL state 字符串映射
std::string AlStateToString(uint8_t al_state) {
  switch (al_state) {
    case 0x01: return "INIT";
    case 0x02: return "PREOP";
    case 0x04: return "SAFEOP";
    case 0x08: return "OP";
    case 0x00: return "NONE";
    default: return "UNKNOWN";
  }
}

// Status word 字符串解析（合理缩写）
std::string StatusToString(uint16_t status) {
  std::string result;
  if (status & 0x0001) result += "Rdy ";
  if (status & 0x0002) result += "On ";
  if (status & 0x0004) result += "Ena ";
  if (status & 0x0008) result += "Flt ";
  if (status & 0x0010) result += "Vol ";
  if (status & 0x0040) result += "Dis ";
  if (status & 0x0100) result += "Tgt ";
  if ((status >> 12) & 1) result += "Ign ";
  if (result.empty()) result = "Idle";
  return result;
}

int main(int argc, char** argv) {
  // 解析参数：duration_ms (0=无限) + 可选 config_path
  int32_t duration_ms = 10000;
  if (argc >= 2) duration_ms = atoi(argv[1]);
  std::string config_path;
  if (argc >= 3) {
    config_path = argv[2];
  } else {
    // 默认查找：开发环境 Config/motor_continuous.yaml，找不到用生产环境 /etc/
    std::ifstream test("Config/motor_continuous.yaml");
    config_path = test.good() ? "Config/motor_continuous.yaml"
                              : "/etc/motor-continuous/config.yaml";
  }

  // 注册信号处理器（用于 Ctrl+C 退出）
  signal(SIGINT, OnSignal);
  signal(SIGTERM, OnSignal);

  // 实时调度：CSP+DC 模式要求周期稳定，否则从站会掉状态 (SAFEOP/INIT/PREOP)
  struct sched_param sp = {};
  sp.sched_priority = sched_get_priority_max(SCHED_FIFO);
  if (sched_setscheduler(0, SCHED_FIFO, &sp) == -1) {
    KAYLORDUT_LOG_WARN("sched_setscheduler 失败: {} (建议用 sudo 运行)", strerror(errno));
  } else {
    KAYLORDUT_LOG_INFO("实时调度已启用: SCHED_FIFO 优先级 {}", sp.sched_priority);
  }
  if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
    KAYLORDUT_LOG_WARN("mlockall 失败: {}", strerror(errno));
  }
  StackPrefault();

  setvbuf(stdout, nullptr, _IONBF, 0);  // 无缓冲输出

  // 读取配置
  GlobalConfig gcfg;
  std::vector<SlaveConfig> slave_cfgs;
  if (!LoadConfig(config_path, gcfg, slave_cfgs)) {
    return 1;
  }

  KAYLORDUT_LOG_INFO("========== Renesas RA8T2 电机持续转动测试 ==========");
  KAYLORDUT_LOG_INFO("配置文件: {}", config_path);
  KAYLORDUT_LOG_INFO("  从站数量: {}", slave_cfgs.size());
  KAYLORDUT_LOG_INFO("  周期: {} ns ({} Hz)", gcfg.cycle_ns, NSEC_PER_SEC / gcfg.cycle_ns);
  KAYLORDUT_LOG_INFO("  DC 激活码: 0x{:X}", gcfg.assign_activate);
  KAYLORDUT_LOG_INFO("  CSP 模式 (0x6060): {}", gcfg.mode_of_operation);
  if (duration_ms == 0) {
    KAYLORDUT_LOG_INFO("  超时时间: 无限 (按 Ctrl+C 退出)");
  } else {
    KAYLORDUT_LOG_INFO("  超时时间: {} ms", duration_ms);
  }
  for (const auto& sc : slave_cfgs) {
    KAYLORDUT_LOG_INFO("  [{}] alias={} pos={} vid=0x{:06X} pid=0x{:04X} step={} ±{}",
              sc.name, sc.alias, sc.position, sc.vendor_id, sc.product_code,
              sc.step, sc.travel_limit);
  }
  KAYLORDUT_LOG_INFO("");
  KAYLORDUT_LOG_INFO("状态缩写说明:");
  KAYLORDUT_LOG_INFO("  AL状态: INIT, PREOP, SAFEOP, OP");
  KAYLORDUT_LOG_INFO("  Status: Rdy(Ready) On(On) Ena(Enabled) Flt(Fault) Vol(Voltage)");
  KAYLORDUT_LOG_INFO("          Dis(Disabled) Tgt(TargetReached) Ign(IgnoringTarget)");
  KAYLORDUT_LOG_INFO("");

  // 初始化 EtherCAT
  ec_master_t* master = ecrt_request_master(0);
  if (!master) { KAYLORDUT_LOG_ERROR("[FATAL] request master 失败"); return 2; }
  ec_domain_t* domain = ecrt_master_create_domain(master);
  if (!domain) { KAYLORDUT_LOG_ERROR("[FATAL] create domain 失败"); return 2; }

  // 准备运行时从站
  std::vector<SlaveRuntime> slaves(slave_cfgs.size());
  for (size_t i = 0; i < slave_cfgs.size(); ++i) {
    slaves[i].cfg = slave_cfgs[i];
  }

  // slave_config + 收集 PDO 注册项
  std::vector<ec_pdo_entry_reg_t> regs;
  regs.reserve(slaves.size() * 6 + 1);
  for (auto& sl : slaves) {
    sl.sc = ecrt_master_slave_config(master, sl.cfg.alias, sl.cfg.position,
                                     sl.cfg.vendor_id, sl.cfg.product_code);
    if (!sl.sc) {
      KAYLORDUT_LOG_ERROR("[FATAL] [{}] slave_config 失败", sl.cfg.name);
      return 2;
    }
    regs.push_back({sl.cfg.alias, sl.cfg.position, sl.cfg.vendor_id, sl.cfg.product_code,
                    0x607a, 0x00, &sl.off_target_position});
    regs.push_back({sl.cfg.alias, sl.cfg.position, sl.cfg.vendor_id, sl.cfg.product_code,
                    0x6040, 0x00, &sl.off_control_word});
    regs.push_back({sl.cfg.alias, sl.cfg.position, sl.cfg.vendor_id, sl.cfg.product_code,
                    0x6064, 0x00, &sl.off_position_actual});
    regs.push_back({sl.cfg.alias, sl.cfg.position, sl.cfg.vendor_id, sl.cfg.product_code,
                    0x6041, 0x00, &sl.off_status_word});
    regs.push_back({sl.cfg.alias, sl.cfg.position, sl.cfg.vendor_id, sl.cfg.product_code,
                    0x606c, 0x00, &sl.off_velocity_actual});
    regs.push_back({sl.cfg.alias, sl.cfg.position, sl.cfg.vendor_id, sl.cfg.product_code,
                    0x6077, 0x00, &sl.off_torque_actual});
  }
  regs.push_back({});  // 结束符
  if (ecrt_domain_reg_pdo_entry_list(domain, regs.data())) {
    KAYLORDUT_LOG_ERROR("[FATAL] register PDO entries 失败");
    return 2;
  }

  // CSP 模式 + DC 配置（每个从站都启用 DC）
  for (auto& sl : slaves) {
    if (ecrt_slave_config_sdo8(sl.sc, 0x6060, 0x00, gcfg.mode_of_operation)) {
      KAYLORDUT_LOG_WARN("[{}] 设置 0x6060 (CSP) 失败", sl.cfg.name);
    }
    ecrt_slave_config_dc(sl.sc, gcfg.assign_activate, gcfg.cycle_ns, 0, 0, 0);
  }

  // 激活 Master
  if (ecrt_master_activate(master)) {
    KAYLORDUT_LOG_ERROR("[FATAL] activate master 失败");
    return 2;
  }
  uint8_t* pd = ecrt_domain_data(domain);
  if (!pd) { KAYLORDUT_LOG_ERROR("[FATAL] get domain data 失败"); return 2; }

  // 初始化 DC 参考时钟
  struct timespec t;
  clock_gettime(CLOCK_MONOTONIC, &t);
  uint64_t app_time = (uint64_t)t.tv_sec * NSEC_PER_SEC + t.tv_nsec;
  ecrt_master_application_time(master, app_time);
  ecrt_master_sync_reference_clock(master);
  ecrt_master_sync_slave_clocks(master);

  // 周期循环初始化
  struct timespec next;
  clock_gettime(CLOCK_MONOTONIC, &next);
  next.tv_sec += 1;
  next.tv_nsec = 0;

  bool op_reached = false;
  bool fault_seen = false;
  const int32_t total_iters = duration_ms + 2000;  // 多给 2s 状态机初始化
  int32_t iter = 0;
  bool infinite_mode = (duration_ms == 0);

  KAYLORDUT_LOG_INFO("========== 开始周期循环 ==========");
  if (infinite_mode) {
    KAYLORDUT_LOG_INFO("无限模式运行 (按 Ctrl+C 退出)");
  }

  while (g_running && (infinite_mode || iter < total_iters)) {
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

    ecrt_master_receive(master);
    ecrt_domain_process(domain);

    bool do_print = (iter % gcfg.print_interval == 0);

    // 遍历每个从站
    for (auto& sl : slaves) {
      uint16_t status = EC_READ_U16(pd + sl.off_status_word);
      int32_t  pos = EC_READ_S32(pd + sl.off_position_actual);
      int32_t  vel = EC_READ_S32(pd + sl.off_velocity_actual);
      int16_t  tor = EC_READ_S16(pd + sl.off_torque_actual);
      sl.final_status = status;

      ec_slave_config_state_t s;
      ecrt_slave_config_state(sl.sc, &s);
      sl.final_al_state = s.al_state;
      if (s.al_state == 8) op_reached = true;
      if (status & 0x0008) fault_seen = true;

      // CiA402 状态机
      uint16_t low = status & 0x6f;
      uint16_t ctrl;
      if (low == 0x40) {
        ctrl = 0x06;
      } else if (low == 0x21 || low == 0x23 || low == 0x33) {
        ctrl = 0x0F;
      } else if (low == 0x27 || low == 0x37) {
        ctrl = 0x0F;
      } else {
        ctrl = 0x0F;
      }
      sl.enabled = (low == 0x27 || low == 0x37);

      if (status & 0x0008) {  // Fault: 交替 fault reset / shutdown
        ctrl = (sl.fault_reset_toggle++ % 2) ? 0x06 : 0x80;
      }

      // 目标策略：往返 ±travel_limit
      int32_t target;
      if (!sl.enabled) {
        target = pos;  // 未使能：跟随当前位置
      } else {
        if (pos >= sl.cfg.travel_limit) {
          sl.direction = -1;
        } else if (pos <= -sl.cfg.travel_limit) {
          sl.direction = 1;
        }
        if (sl.direction != sl.last_direction) {
          KAYLORDUT_LOG_WARN("[{}] 到达位置限位! pos={} 方向切换: {} -> {}",
                    sl.cfg.name, pos,
                    sl.last_direction > 0 ? "+" : "-",
                    sl.direction > 0 ? "+" : "-");
          sl.last_direction = sl.direction;
        }
        target = pos + sl.direction * sl.cfg.step;

        if (!sl.initial_pos_recorded) {
          sl.initial_pos = pos;
          sl.min_pos = sl.max_pos = pos;
          sl.initial_pos_recorded = true;
          sl.last_direction = sl.direction;
          KAYLORDUT_LOG_INFO("[{}] 电机使能! 初始位置: {}, 往返范围: [{} ~ {}]",
                    sl.cfg.name, pos, -sl.cfg.travel_limit, sl.cfg.travel_limit);
        } else {
          if (pos > sl.max_pos) sl.max_pos = pos;
          if (pos < sl.min_pos) sl.min_pos = pos;
        }
      }

      // 打印
      if (do_print) {
        std::string al_str = AlStateToString(s.al_state);
        std::string status_str = StatusToString(status);
        const char* dir_str = (sl.direction > 0) ? "↑" : "↓";
        KAYLORDUT_LOG_INFO("[{}] [t={:5}ms] AL={} Status=[{}] pos={} target={} vel={} tor={} {}",
                  sl.cfg.name, iter, al_str, status_str, pos, target, vel, tor, dir_str);
      }

      // 写 PDO
      EC_WRITE_U16(pd + sl.off_control_word, ctrl);
      EC_WRITE_S32(pd + sl.off_target_position, target);
    }

    // DC 时钟同步（全局）
    clock_gettime(CLOCK_MONOTONIC, &t);
    app_time = (uint64_t)t.tv_sec * NSEC_PER_SEC + t.tv_nsec;
    ecrt_master_application_time(master, app_time);
    ecrt_master_sync_reference_clock(master);
    ecrt_master_sync_slave_clocks(master);

    ecrt_domain_queue(domain);
    ecrt_master_send(master);

    next.tv_nsec += gcfg.cycle_ns;
    while (next.tv_nsec >= NSEC_PER_SEC) {
      next.tv_nsec -= NSEC_PER_SEC;
      next.tv_sec++;
    }
    iter++;
  }

  // ===== 测试结果 =====
  KAYLORDUT_LOG_INFO("========== 测试结果 ==========");
  KAYLORDUT_LOG_INFO("状态缩写说明:");
  KAYLORDUT_LOG_INFO("  AL: INIT, PREOP, SAFEOP, OP");
  KAYLORDUT_LOG_INFO("  Status: Rdy(Ready) On(On) Ena(Enabled) Flt(Fault) Vol(Voltage)");
  KAYLORDUT_LOG_INFO("          Dis(Disabled) Tgt(TargetReached) Ign(IgnoringTarget)");
  KAYLORDUT_LOG_INFO("");
  KAYLORDUT_LOG_INFO("运行时间: {} ms, 实际循环: {} 次", duration_ms, iter);

  for (const auto& sl : slaves) {
    std::string al_str = AlStateToString(sl.final_al_state);
    std::string status_str = StatusToString(sl.final_status);
    KAYLORDUT_LOG_INFO("[{}] AL={} Status=[{}]", sl.cfg.name, al_str, status_str);
    if (sl.initial_pos_recorded) {
      int32_t travel = sl.max_pos - sl.initial_pos;
      int32_t total_range = sl.max_pos - sl.min_pos;
      bool moved = (travel > 1000) || (sl.max_pos != sl.initial_pos);
      KAYLORDUT_LOG_INFO("    初始位置: {}, 范围: [{} ~ {}], 净行程: {}, 总范围: {}",
                sl.initial_pos, sl.min_pos, sl.max_pos, travel, total_range);
      if (moved) {
        KAYLORDUT_LOG_INFO("    >>> 结论: MOTOR_ROTATED (净行程={})", travel);
      } else {
        KAYLORDUT_LOG_INFO("    >>> 结论: NOT_WORKING");
      }
    } else {
      KAYLORDUT_LOG_INFO("    行程统计: 未记录（该从站从未使能）");
      KAYLORDUT_LOG_INFO("    >>> 结论: NOT_WORKING");
    }
  }
  KAYLORDUT_LOG_INFO("整体: OP={} fault={}", op_reached ? "YES" : "NO",
            fault_seen ? "YES" : "NO");

  // 清理
  ecrt_master_deactivate(master);
  ecrt_release_master(master);
  return 0;
}
