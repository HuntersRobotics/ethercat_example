//
// motor_continuous - Renesas RA8T2 CiA402 电机持续转动测试程序
// Copyright (c) 2026 kaylorchen
// SPDX-License-Identifier: TBD
//
// 用法:
//   motor_continuous <duration_ms>
//   例: motor_continuous 10000    (运行 10 秒)
//       motor_continuous 0         (无限运行，Ctrl+C 退出)
//
// 功能:
//   - 硬件: Renesas EtherCAT RA8T2 CiA402 (Vendor 0x00000766, Product 0x00000802)
//   - DC 激活码 0x0301, 1ms 周期 (1000Hz), CSP 模式
//   - 目标策略: 在 ±100万 边界往返 (每周期 ±100)
//   - 实时调度 SCHED_FIFO (需 sudo)
//

#include <ecrt.h>

#include <sched.h>
#include <sys/mman.h>

#include <cstdint>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <string>

#include "kaylordut/log/logger.h"

// 固定配置 - 宏定义
#define ASSIGN_ACTIVATE 0x0301      // DC 激活码（已验证能让电机转动）
#define CYCLE_NS 1000000            // 1ms 周期 = 1000Hz
#define VENDOR_ID    0x00000766u
#define PRODUCT_CODE 0x00000802u
#define SLAVE_POS    0
#define NSEC_PER_SEC 1000000000L

static bool g_running = true;
static void OnSignal(int) { g_running = false; }

// 实时调度相关：预先触发栈页错误，避免运行时缺页延迟
#define MAX_SAFE_STACK (8 * 1024)
static void StackPrefault() {
  unsigned char dummy[MAX_SAFE_STACK];
  memset(dummy, 0, MAX_SAFE_STACK);
}

struct PdoOffset {
  uint32_t target_position;
  uint32_t control_word;
  uint32_t position_actual;
  uint32_t status_word;
  uint32_t velocity_actual;
  uint32_t torque_actual;
};

// 位置限位
static const int32_t kSafetyTravelLimit = 10000;  // 位置往返边界 (±1万)

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
  if (status & 0x0001) result += "Rdy ";      // Ready
  if (status & 0x0002) result += "On ";        // Switched On
  if (status & 0x0004) result += "Ena ";      // Enabled
  if (status & 0x0008) result += "Flt ";      // Fault
  if (status & 0x0010) result += "Vol ";      // Voltage Enabled
  if (status & 0x0040) result += "Dis ";      // Disabled
  if (status & 0x0100) result += "Tgt ";      // Target Reached
  if ((status >> 12) & 1) result += "Ign ";    // Ignoring Target
  if (result.empty()) result = "Idle";
  return result;
}

int main(int argc, char** argv) {
  // 解析超时时间参数（0 = 无限运行）
  int32_t duration_ms = 10000;  // 默认 10 秒
  if (argc >= 2) duration_ms = atoi(argv[1]);

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

  KAYLORDUT_LOG_INFO("========== Renesas RA8T2 电机持续转动测试 ==========");
  KAYLORDUT_LOG_INFO("配置参数:");
  KAYLORDUT_LOG_INFO("  DC 激活码: 0x{:X}", ASSIGN_ACTIVATE);
  KAYLORDUT_LOG_INFO("  周期: {} ns ({} ms = {} Hz)", CYCLE_NS, CYCLE_NS/1000000, 1000000000/CYCLE_NS);
  if (duration_ms == 0) {
    KAYLORDUT_LOG_INFO("  超时时间: 无限 (按 Ctrl+C 退出)");
  } else {
    KAYLORDUT_LOG_INFO("  超时时间: {} ms", duration_ms);
  }
  KAYLORDUT_LOG_INFO("  目标策略: 在 ±{} 之间往返 (每循环 ±100)", kSafetyTravelLimit);
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
  ec_slave_config_t* sc =
      ecrt_master_slave_config(master, 0, 0, VENDOR_ID, PRODUCT_CODE);
  if (!sc) { KAYLORDUT_LOG_ERROR("[FATAL] slave config 失败"); return 2; }

  // PDO 配置
  PdoOffset off{};
  ec_pdo_entry_reg_t regs[] = {
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x607a, 0x00, &off.target_position},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6040, 0x00, &off.control_word},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6064, 0x00, &off.position_actual},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6041, 0x00, &off.status_word},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x606c, 0x00, &off.velocity_actual},
      {0, 0, VENDOR_ID, PRODUCT_CODE, 0x6077, 0x00, &off.torque_actual},
      {}};
  if (ecrt_domain_reg_pdo_entry_list(domain, regs)) {
    KAYLORDUT_LOG_ERROR("[FATAL] register PDO entries 失败");
    return 2;
  }

  // 设置 CSP 模式
  if (ecrt_slave_config_sdo8(sc, 0x6060, 0x00, 8)) {
    KAYLORDUT_LOG_WARN("设置 0x6060 (CSP) 失败");
  }

  // 配置 DC (使用固定激活码)
  KAYLORDUT_LOG_INFO("配置 DC: ecrt_slave_config_dc(0x{:X}, {} ns)", ASSIGN_ACTIVATE, CYCLE_NS);
  ecrt_slave_config_dc(sc, ASSIGN_ACTIVATE, CYCLE_NS, 0, 0, 0);

  // 激活 Master
  if (ecrt_master_activate(master)) {
    KAYLORDUT_LOG_ERROR("[FATAL] activate master 失败");
    return 2;
  }

  // 获取数据指针
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

  // 状态变量
  bool enabled = false;
  bool op_reached = false;
  bool fault_seen = false;
  bool initial_pos_recorded = false;  // 是否已记录初始位置
  int32_t initial_pos = 0;
  int32_t min_pos = 0;
  int32_t max_pos = 0;
  int32_t direction = 1;       // 移动方向: 1=正向(+100), -1=反向(-100)
  int32_t last_direction = 1;  // 上次方向（用于检测切换）
  uint16_t final_status = 0;
  uint16_t final_al_state = 0;
  int32_t fault_reset_toggle = 0;

  const int32_t total_iters = duration_ms + 2000;  // 多给 2s 状态机初始化
  int32_t iter = 0;
  bool infinite_mode = (duration_ms == 0);  // 无限模式标志

  KAYLORDUT_LOG_INFO("========== 开始周期循环 ==========");
  if (infinite_mode) {
    KAYLORDUT_LOG_INFO("无限模式运行 (按 Ctrl+C 退出)");
  }

  while (g_running && (infinite_mode || iter < total_iters)) {
    // 等待下一个周期时间点
    clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &next, nullptr);

    // 接收并处理 PDO
    ecrt_master_receive(master);
    ecrt_domain_process(domain);

    // 读取状态和位置
    uint16_t status = EC_READ_U16(pd + off.status_word);
    int32_t  pos = EC_READ_S32(pd + off.position_actual);
    int32_t  vel = EC_READ_S32(pd + off.velocity_actual);  // 实际速度
    int16_t  tor = EC_READ_S16(pd + off.torque_actual);    // 实际扭矩 (单位 0.001 Nm)
    final_status = status;

    // 检查从站状态
    ec_slave_config_state_t s;
    ecrt_slave_config_state(sc, &s);
    final_al_state = s.al_state;
    if (s.al_state == 8) op_reached = true;
    if (status & 0x0008) fault_seen = true;

    // 完整的状态机处理（CiA402）
    uint16_t low = status & 0x6f;
    uint16_t ctrl;

    if (low == 0x40) {
      ctrl = 0x06;             // Switch on disabled → Shutdown
    } else if (low == 0x21 || low == 0x23 || low == 0x33) {
      ctrl = 0x0F;             // Ready/Switched on → Enable operation
    } else if (low == 0x27 || low == 0x37) {
      ctrl = 0x0F;             // Operation enabled
    } else {
      ctrl = 0x0F;
    }
    // 使能状态实时跟随 status（掉状态时自动回退为 false）
    enabled = (low == 0x27 || low == 0x37);

    if (status & 0x0008) {     // Fault: 交替写 fault reset / shutdown
      ctrl = (fault_reset_toggle++ % 2) ? 0x06 : 0x80;
    }

    // 目标策略：在 ±100万 之间往返（三角波）
    int32_t target;
    if (!enabled) {
      // 未使能时：跟随当前位置
      target = pos;
    } else {
      // **往返策略：到达边界后反向**
      // 位置 >= +100万 → 反向减100
      // 位置 <= -100万 → 正向加100
      if (pos >= kSafetyTravelLimit) {
        direction = -1;  // 到达上限，反向
      } else if (pos <= -kSafetyTravelLimit) {
        direction = 1;   // 到达下限，正向
      }

      // 方向切换时打 warning（只在切换瞬间，避免刷屏）
      if (direction != last_direction) {
        KAYLORDUT_LOG_WARN("到达位置限位! pos={} 方向切换: {} -> {}",
                   pos,
                   last_direction > 0 ? "+100" : "-100",
                   direction > 0 ? "+100" : "-100");
        last_direction = direction;
      }
      target = pos + direction * 100;  // 按当前方向移动100

      // 更新统计（只在使能后统计）
      if (!initial_pos_recorded) {
        // 第一次使能时记录初始位置 - 这才是真正的开始！
        initial_pos = pos;
        min_pos = max_pos = pos;
        initial_pos_recorded = true;
        last_direction = direction;  // 初始化方向记录
        KAYLORDUT_LOG_INFO("电机使能! 初始位置: {}, 往返范围: [{} ~ {}]",
                   initial_pos, -kSafetyTravelLimit, kSafetyTravelLimit);
      } else {
        // 后续位置更新（只在这里统计，避免在未使能时统计）
        if (pos > max_pos) max_pos = pos;
        if (pos < min_pos) min_pos = pos;
      }
    }

    // 每 100 次循环打印一次状态（1秒）—— 显示当前位置和目标位置
    if (iter % 100 == 0) {
      uint8_t al_state = s.al_state;  // bit-field 需要先复制到临时变量
      std::string al_str = AlStateToString(al_state);
      std::string status_str = StatusToString(status);
      const char* dir_str = (direction > 0) ? "↑" : "↓";  // 方向指示
      KAYLORDUT_LOG_INFO("[t={:5}ms] AL={} Status=[{}] pos={} target={} vel={} tor={} {}{}{}",
                iter, al_str, status_str, pos, target, vel, tor, dir_str,
                enabled ? " ENABLED" : "",
                fault_seen ? " FAULT" : "");
    }

    // 写控制字和目标位置
    EC_WRITE_U16(pd + off.control_word, ctrl);
    EC_WRITE_S32(pd + off.target_position, target);

    // DC 时钟同步
    clock_gettime(CLOCK_MONOTONIC, &t);
    app_time = (uint64_t)t.tv_sec * NSEC_PER_SEC + t.tv_nsec;
    ecrt_master_application_time(master, app_time);
    ecrt_master_sync_reference_clock(master);
    ecrt_master_sync_slave_clocks(master);

    // 发送 PDO 数据
    ecrt_domain_queue(domain);
    ecrt_master_send(master);

    // 更新下一个周期时间
    next.tv_nsec += CYCLE_NS;
    while (next.tv_nsec >= NSEC_PER_SEC) {
      next.tv_nsec -= NSEC_PER_SEC;
      next.tv_sec++;
    }

    iter++;
  }

  // 结果输出
  std::string final_al_str = AlStateToString(final_al_state);
  std::string final_status_str = StatusToString(final_status);

  KAYLORDUT_LOG_INFO("========== 测试结果 ==========");
  KAYLORDUT_LOG_INFO("状态缩写说明:");
  KAYLORDUT_LOG_INFO("  AL: INIT, PREOP, SAFEOP, OP");
  KAYLORDUT_LOG_INFO("  Status: Rdy(Ready) On(On) Ena(Enabled) Flt(Fault) Vol(Voltage)");
  KAYLORDUT_LOG_INFO("          Dis(Disabled) Tgt(TargetReached) Ign(IgnoringTarget)");
  KAYLORDUT_LOG_INFO("");
  KAYLORDUT_LOG_INFO("运行时间: {} ms", duration_ms);
  KAYLORDUT_LOG_INFO("实际循环: {} 次", iter);
  KAYLORDUT_LOG_INFO("AL state: {} (OP={})", final_al_str, op_reached ? "YES" : "NO");
  KAYLORDUT_LOG_INFO("最终状态: [{}]", final_status_str);

  if (initial_pos_recorded) {
    // 电机使能过，显示有效行程统计
    int32_t travel = max_pos - initial_pos;  // 从初始位置开始的净行程
    int32_t total_range = max_pos - min_pos;  // 最大范围（包含往返）
    bool motor_moved = (travel > 1000) || (max_pos != initial_pos);

    KAYLORDUT_LOG_INFO("初始位置: {} (使能后)", initial_pos);
    KAYLORDUT_LOG_INFO("位置范围: [{} ~ {}]", min_pos, max_pos);
    KAYLORDUT_LOG_INFO("净行程: {} (当前位置 - 初始位置)", travel);
    KAYLORDUT_LOG_INFO("总范围: {} (最大值 - 最小值)", total_range);

    // 结论
    if (motor_moved) {
      KAYLORDUT_LOG_INFO(">>> 结论: MOTOR_ROTATED (净行程={}) — 电机持续转动成功!", travel);
    } else {
      KAYLORDUT_LOG_INFO(">>> 结论: NOT_WORKING (enabled={}, OP={}, fault={})",
                enabled ? 1 : 0, op_reached ? 1 : 0, fault_seen ? 1 : 0);
    }
  } else {
    // 电机从未使能，位置统计无效
    KAYLORDUT_LOG_INFO("行程统计: 未记录（电机从未使能，位置无效）");
    KAYLORDUT_LOG_INFO(">>> 结论: NOT_WORKING (enabled=0, OP={}, fault={})",
              op_reached ? 1 : 0, fault_seen ? 1 : 0);
  }

  // 清理
  ecrt_master_deactivate(master);
  ecrt_release_master(master);

  return 0;
}
