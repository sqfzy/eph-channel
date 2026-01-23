#pragma once

#include <iostream>
#include <pthread.h>
#include <sched.h>

namespace benchmark {

class System {
public:
  static void pin_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    if (pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset) !=
        0) {
      perror("pthread_setaffinity_np");
    } else {
      std::cout << "[System] Thread pinned to core " << core_id << std::endl;
    }
  }

  static void set_realtime_priority(int priority = 99) {
    sched_param param;
    param.sched_priority = priority;
    if (pthread_setschedparam(pthread_self(), SCHED_FIFO, &param) != 0) {
      std::cerr << "[System] WARNING: Failed to set SCHED_FIFO. Run with sudo?"
                << std::endl;
    } else {
      std::cout << "[System] SCHED_FIFO priority " << priority << " enabled."
                << std::endl;
    }
  }

  static void disable_turbo_boost() {
    // 仅供参考，需要 root 权限
    std::cout << "[System] Hint: Disable turbo boost for stable results:\n"
              << "  echo 1 | sudo tee "
                 "/sys/devices/system/cpu/intel_pstate/no_turbo"
              << std::endl;
  }
};

} // namespace benchmark