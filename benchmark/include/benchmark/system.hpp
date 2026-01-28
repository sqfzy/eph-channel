#pragma once

#include <iostream>
#include <numa.h>
#include <pthread.h>
#include <sched.h>

namespace benchmark {

class System {
public:
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

  static void bind_numa(int node, int core_id) {
    // 1. 检查 NUMA 是否可用
    if (numa_available() < 0) {
      std::cerr << "[System] ERROR: NUMA not available on this system."
                << std::endl;
      return;
    }

    // 2. 校验 CPU 与节点的物理拓扑关系
    int actual_node = numa_node_of_cpu(core_id);
    if (actual_node != node) {
      std::cerr << "[System] WARNING: Topology mismatch! Core " << core_id
                << " is physically on NUMA node " << actual_node
                << ", but you are binding to node " << node << "." << std::endl;
    }

    // 3. 绑定内存策略
    struct bitmask *nodemask = numa_allocate_nodemask();
    numa_bitmask_setbit(nodemask, node);
    numa_set_membind(nodemask);
    numa_free_nodemask(nodemask);

    // 4. 绑定 CPU 亲和性
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
      perror("[System] ERROR: sched_setaffinity failed");
    } else {
      std::cout << "[System] Successfully bound to NUMA node " << node
                << " and CPU core " << core_id << "." << std::endl;
    }
  }
};

} // namespace benchmark
