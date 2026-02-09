#pragma once

#include <vector>
#include <string>
#include <fstream>
#include <regex>
#include <algorithm>
#include <thread>
#include <stdexcept>

#if defined(__linux__)
#include <pthread.h>
#include <sched.h>
#endif

namespace eph::benchmark {

struct CpuTopologyInfo {
    unsigned socket_id;      // 物理 CPU Socket
    unsigned core_id;        // 物理核心
    unsigned hw_thread_id;   // 硬件线程（超线程）
};

// 解析 /proc/cpuinfo 获取 CPU 拓扑
inline std::vector<CpuTopologyInfo> get_cpu_topology() {
    std::vector<CpuTopologyInfo> cpus;

#if defined(__linux__)
    std::regex const res[3] = {
        std::regex(R"(physical id\s+:\s+(\d+))"),
        std::regex(R"(core id\s+:\s+(\d+))"),
        std::regex(R"(processor\s+:\s+(\d+))")
    };

    std::ifstream cpuinfo("/proc/cpuinfo");
    std::smatch m;
    CpuTopologyInfo element{};
    unsigned valid_mask = 0;

    for (std::string line; getline(cpuinfo, line);) {
        for (unsigned i = 0; i < 3; ++i) {
            if ((valid_mask & (1 << i)) || !std::regex_match(line, m, res[i]))
                continue;

            unsigned value = std::stoul(m[1]);
            if (i == 0) element.socket_id = value;
            else if (i == 1) element.core_id = value;
            else element.hw_thread_id = value;

            valid_mask |= (1 << i);
            if (valid_mask == 7) { // 全部收集完毕
                cpus.push_back(element);
                valid_mask = 0;
            }
            break;
        }
    }

    if (cpus.size() != std::thread::hardware_concurrency()) {
        throw std::runtime_error("CPU topology detection failed");
    }

    // 按 hw_thread_id 排序
    std::sort(cpus.begin(), cpus.end(), [](auto& a, auto& b) {
        return a.hw_thread_id < b.hw_thread_id;
    });
#else
    // macOS/Windows 回退方案
    for (unsigned i = 0; i < std::thread::hardware_concurrency(); ++i) {
        cpus.push_back({0, i, i});
    }
#endif

    return cpus;
}

// 按物理核心排序（同一 Socket 的核心聚合在一起）
inline std::vector<CpuTopologyInfo> sort_by_core(std::vector<CpuTopologyInfo> v) {
    std::sort(v.begin(), v.end(), [](auto& a, auto& b) {
        return std::tie(a.socket_id, a.core_id, a.hw_thread_id) <
               std::tie(b.socket_id, b.core_id, b.hw_thread_id);
    });
    return v;
}

// 绑定线程到指定 CPU
inline void set_thread_affinity(unsigned cpu_id) {
#if defined(__linux__)
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);
#elif defined(__APPLE__)
    // macOS 不支持硬亲和性，只能设置 QoS
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
}

// 获取 CPU 基准频率（用于 TSC 校准）
inline double get_cpu_base_frequency_ghz() {
#if defined(__linux__)
    std::regex re(R"(model name\s*:[^@]+@\s*([\d.]+)\s*GHz)");
    std::ifstream cpuinfo("/proc/cpuinfo");
    std::smatch m;
    for (std::string line; getline(cpuinfo, line);) {
        if (std::regex_match(line, m, re)) {
            return std::stod(m[1]);
        }
    }
#endif
    return 1.0; // 回退值
}

} // namespace eph::benchmark
