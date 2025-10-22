#include <kaylordut/log/logger.h>
#include <sys/mman.h>

#include <csignal>
#include <thread>

#include "app.h"
#include "string"

#define PERIOD_NS (1000000)

#define MAX_SAFE_STACK                          \
  (8 * 1024) /* The maximum stack size which is \
guranteed safe to access without                \
faulting */

/* Constants */
#define NSEC_PER_SEC (1000000000)
#define FREQUENCY (NSEC_PER_SEC / PERIOD_NS)

static bool running = true;
void signalHandler(int sig) {
  KAYLORDUT_LOG_INFO("Caught signal {}", sig);
  running = false;
}
void stack_prefault(void) {
  unsigned char dummy[MAX_SAFE_STACK];

  memset(dummy, 0, MAX_SAFE_STACK);
}

int main(int argc, const char *argv[]) {
  //初始化
  std::string cmd;
  for (size_t i = 0; i < argc; i++) {
    cmd += " ";
    cmd += argv[i];
  }
  KAYLORDUT_LOG_INFO("command = {}", cmd);
  struct sigaction sa;
  sa.sa_handler = signalHandler;
  sigemptyset(&sa.sa_mask);
  sa.sa_flags = 0;
  sigaction(SIGINT, &sa, NULL);

  /* Set priority */

  struct sched_param param = {};
  param.sched_priority = sched_get_priority_max(SCHED_FIFO);

  KAYLORDUT_LOG_INFO("Using priority {}.", param.sched_priority);
  if (sched_setscheduler(0, SCHED_FIFO, &param) == -1) {
    KAYLORDUT_LOG_ERROR("sched_setscheduler failed");
  }

  /* Lock memory */

  if (mlockall(MCL_CURRENT | MCL_FUTURE) == -1) {
    KAYLORDUT_LOG_ERROR("Warning: Failed to lock memory: {}", strerror(errno));
  }

  stack_prefault();

  KAYLORDUT_LOG_INFO("Starting RT task with dt={} ns.", PERIOD_NS);
  struct timespec wakeup_time;

  clock_gettime(CLOCK_MONOTONIC, &wakeup_time);
  wakeup_time.tv_sec += 1; /* start in future */
  wakeup_time.tv_nsec = 0;
  int ret = 0;

  //创建ETHEAECAT应用对象APP
  App app(&running);
  app.Config();
  app.CheckMasterState();
  // app.InitializeDevices();
  while (running) {
    ret = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &wakeup_time, NULL);
    if (ret) {
      KAYLORDUT_LOG_INFO("clock_nanosleep(): {}", strerror(ret));
      break;
    }
    app.RunOnce();
    wakeup_time.tv_nsec += PERIOD_NS;
    while (wakeup_time.tv_nsec >= NSEC_PER_SEC) {
      wakeup_time.tv_nsec -= NSEC_PER_SEC;
      wakeup_time.tv_sec++;
    }
  }
  KAYLORDUT_LOG_INFO("exit main");
  return 0;
}
