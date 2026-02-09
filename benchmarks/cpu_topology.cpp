#include "eph/benchmark/cpu_topology.hpp"
#include "eph/benchmark/timer.hpp"
#include "eph/core/queue.hpp"
#include "eph/platform.hpp"
#include <print>
#include <format>
#include <fstream>
#include <thread>
#include <barrier>
#include <optional>
#include <vector>

using namespace eph;
using namespace eph::benchmark;

// 测试不同 CPU 拓扑下的性能
enum class TopologyMode {
    HyperThread,    // 同一物理核的两个逻辑线程
    SameSocket,     // 同一 Socket 的不同物理核
    CrossSocket,    // 跨 Socket（NUMA 节点）
};

// 简单的结果结构
struct TopologyBenchResult {
    TopologyMode mode;
    double throughput_msg_per_sec;
    double latency_ns_per_msg; // 改为每条消息的系统耗时 (inverse throughput)
};

template<typename Queue>
class TopologyBenchmark {
    // 正式测试消息量
    static constexpr size_t RUN_MESSAGES = 10'000'000;
    // 热身消息量
    static constexpr size_t WARMUP_MESSAGES = 2'000'000;

public:
    // 返回 optional，如果拓扑不支持该模式则返回 nullopt
    static std::optional<TopologyBenchResult> run(
        TopologyMode mode,
        const std::vector<CpuTopologyInfo>& topology,
        bool is_warmup = false
    ) {
        auto cpus_opt = select_cpus(mode, topology);
        
        if (!cpus_opt) {
            if (!is_warmup) {
                std::println(stderr, "Skipping {} mode: Hardware requirement not met (e.g., single socket system).", 
                             mode_name(mode));
            }
            return std::nullopt;
        }

        auto [cpu1, cpu2] = *cpus_opt;
        size_t count = is_warmup ? WARMUP_MESSAGES : RUN_MESSAGES;

        if (!is_warmup) {
            std::println("Testing {} mode: CPU {} <-> CPU {}", 
                         mode_name(mode), cpu1, cpu2);
        }

        Queue queue;
        std::barrier sync(2);

        uint64_t producer_start = 0, producer_end = 0;
        uint64_t consumer_start = 0, consumer_end = 0;

        // Consumer thread
        std::jthread consumer([&] {
            set_thread_affinity(cpu2);
            sync.arrive_and_wait(); // Wait for producer to be ready

            uint32_t value;
            consumer_start = TSC::now();
            for (size_t i = 0; i < count; ++i) {
                while (!queue.try_pop(value)) {
                    cpu_relax();
                }
                do_not_optimize(value);
            }
            consumer_end = TSC::now();
        });

        // Producer thread
        set_thread_affinity(cpu1);
        sync.arrive_and_wait(); // Wait for consumer to be ready

        producer_start = TSC::now();
        for (size_t i = 0; i < count; ++i) {
            while (!queue.try_push(static_cast<uint32_t>(i))) {
                cpu_relax();
            }
        }
        producer_end = TSC::now();

        consumer.join();

        if (is_warmup) return std::nullopt;

        // 计算指标
        // 使用整个系统的最大耗时窗口作为总耗时
        uint64_t start_cycles = std::min(producer_start, consumer_start);
        uint64_t end_cycles = std::max(producer_end, consumer_end);
        uint64_t total_cycles = end_cycles - start_cycles;

        double freq_ghz = get_cpu_base_frequency_ghz();
        double elapsed_sec = total_cycles / (freq_ghz * 1e9);
        
        double throughput = count / elapsed_sec;
        // Latency = 1 / Throughput (ns/msg)
        // 反映了系统处理一条消息的平均间隔时间，包含了流水线停顿
        double latency_ns = (elapsed_sec * 1e9) / count;

        return TopologyBenchResult{
            .mode = mode,
            .throughput_msg_per_sec = throughput,
            .latency_ns_per_msg = latency_ns
        };
    }

    static const char* mode_name(TopologyMode mode) {
        switch (mode) {
            case TopologyMode::HyperThread: return "HyperThread";
            case TopologyMode::SameSocket: return "SameSocket";
            case TopologyMode::CrossSocket: return "CrossSocket";
        }
        return "Unknown";
    }

private:
    static std::optional<std::pair<unsigned, unsigned>> select_cpus(
        TopologyMode mode,
        const std::vector<CpuTopologyInfo>& topology
    ) {
        switch (mode) {
            case TopologyMode::HyperThread: {
                // 找同一个 socket + core 的两个 hw_thread
                for (size_t i = 0; i < topology.size(); ++i) {
                    for (size_t j = i + 1; j < topology.size(); ++j) {
                        if (topology[i].socket_id == topology[j].socket_id &&
                            topology[i].core_id == topology[j].core_id) {
                            return std::make_pair(topology[i].hw_thread_id, topology[j].hw_thread_id);
                        }
                    }
                }
                break;
            }
            case TopologyMode::SameSocket: {
                // 同 socket 不同 core
                for (size_t i = 0; i < topology.size(); ++i) {
                    for (size_t j = i + 1; j < topology.size(); ++j) {
                        if (topology[i].socket_id == topology[j].socket_id &&
                            topology[i].core_id != topology[j].core_id) {
                            return std::make_pair(topology[i].hw_thread_id, topology[j].hw_thread_id);
                        }
                    }
                }
                break;
            }
            case TopologyMode::CrossSocket: {
                // 不同 socket
                for (size_t i = 0; i < topology.size(); ++i) {
                    for (size_t j = i + 1; j < topology.size(); ++j) {
                        if (topology[i].socket_id != topology[j].socket_id) {
                            return std::make_pair(topology[i].hw_thread_id, topology[j].hw_thread_id);
                        }
                    }
                }
                // 不要回退，直接返回 nullopt
                break;
            }
        }
        return std::nullopt;
    }
};

int main() {
    TSC::init();
    auto topology = get_cpu_topology();
    
    // 假设 BoundedQueue 已经定义或引入
    using Queue = BoundedQueue<uint32_t, 4096>;

    std::println("=== CPU Topology Sensitivity Test ===\n");
    
    // 1. Warm-up Phase
    std::println("[Warm-up] Running initial cycles to wake up CPUs...");
    TopologyBenchmark<Queue>::run(TopologyMode::SameSocket, topology, true);
    std::println("[Warm-up] Completed.\n");

    // 2. Benchmark Phase
    std::ofstream csv("outputs/topology_bench.csv");
    csv << "Mode,Throughput(msg/s),Latency(ns/msg)\n";

    for (auto mode : {TopologyMode::HyperThread, 
                      TopologyMode::SameSocket, 
                      TopologyMode::CrossSocket}) {
        
        auto result_opt = TopologyBenchmark<Queue>::run(mode, topology);
        
        if (result_opt) {
            auto& result = *result_opt;
            // 写入 CSV
            csv << std::format("{},{:.0f},{:.2f}\n",
                               TopologyBenchmark<Queue>::mode_name(mode),
                               result.throughput_msg_per_sec,
                               result.latency_ns_per_msg);

            // 打印控制台
            std::println("Mode: {:<12} | Throughput: {:>6.2f} Mmsg/s | Latency: {:>6.2f} ns/msg",
                         TopologyBenchmark<Queue>::mode_name(mode),
                         result.throughput_msg_per_sec / 1e6,
                         result.latency_ns_per_msg);
        }
    }

    std::println("\nResults saved to outputs/topology_bench.csv");
    return 0;
}
