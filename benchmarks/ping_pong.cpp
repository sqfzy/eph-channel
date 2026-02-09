#include "eph/benchmark/cpu_topology.hpp"
#include "eph/benchmark/timer.hpp"
#include "eph/core/queue.hpp"
#include <print>
#include <format>
#include <thread>
#include <barrier>
#include <algorithm>
#include <numeric>
#include <cmath>

using namespace eph;
using namespace eph::benchmark;

// 高精度乒乓延迟测试
template<typename Queue>
class PingPongBenchmark {
    static constexpr size_t PING_PONGS = 100'000;
    static constexpr size_t WARMUP = 1'000;
    static constexpr size_t RUNS = 33; // 多轮取统计

public:
    struct Result {
        double min_rtt_ns;
        double max_rtt_ns;
        double median_rtt_ns;
        double p99_rtt_ns;
        std::vector<double> all_rtts; // 保存原始数据供可视化
    };

    static Result run(unsigned cpu1, unsigned cpu2) {
        std::vector<double> run_medians;

        for (size_t run = 0; run < RUNS; ++run) {
            Queue q1, q2; // 两个队列形成双向通道
            std::barrier sync(2);
            std::vector<uint64_t> latencies(PING_PONGS);

            // Responder (线程 B)
            std::jthread responder([&] {
                set_thread_affinity(cpu2);
                sync.arrive_and_wait();

                // Warmup
                for (size_t i = 0; i < WARMUP; ++i) {
                    uint32_t val = q1.pop();
                    q2.push(val);
                }

                // Measurement
                for (size_t i = 0; i < PING_PONGS; ++i) {
                    uint32_t val = q1.pop();
                    q2.push(val);
                }
            });

            // Initiator (主线程)
            set_thread_affinity(cpu1);
            sync.arrive_and_wait();

            // Warmup
            for (size_t i = 0; i < WARMUP; ++i) {
                q1.push(static_cast<uint32_t>(i));
                uint32_t reply = q2.pop();
                do_not_optimize(reply);
            }

            // Measurement
            for (size_t i = 0; i < PING_PONGS; ++i) {
                auto start = TSC::now();
                q1.push(static_cast<uint32_t>(i));
                uint32_t reply = q2.pop();
                latencies[i] = TSC::now() - start;
                do_not_optimize(reply);
            }

            responder.join();

            // 转换为纳秒
            double freq_ghz = get_cpu_base_frequency_ghz();
            std::vector<double> ns_latencies(PING_PONGS);
            for (size_t i = 0; i < PING_PONGS; ++i) {
                ns_latencies[i] = latencies[i] / freq_ghz;
            }

            std::sort(ns_latencies.begin(), ns_latencies.end());
            run_medians.push_back(ns_latencies[PING_PONGS / 2]);
        }

        // 从所有 runs 中选择最佳（最小 median）的那一轮
        size_t best_run = std::distance(run_medians.begin(),
                                        std::min_element(run_medians.begin(), run_medians.end()));

        // 重新跑一次最佳条件收集完整数据（或者保存所有轮次）
        // 这里简化：直接使用最后一轮的数据
        Queue q1, q2;
        std::barrier sync(2);
        std::vector<uint64_t> final_latencies(PING_PONGS);

        std::jthread responder([&] {
            set_thread_affinity(cpu2);
            sync.arrive_and_wait();
            for (size_t i = 0; i < WARMUP; ++i) {
                q1.pop(); q2.push(0);
            }
            for (size_t i = 0; i < PING_PONGS; ++i) {
                q1.pop(); q2.push(0);
            }
        });

        set_thread_affinity(cpu1);
        sync.arrive_and_wait();
        for (size_t i = 0; i < WARMUP; ++i) {
            q1.push(0); q2.pop();
        }
        for (size_t i = 0; i < PING_PONGS; ++i) {
            auto start = TSC::now();
            q1.push(0);
            q2.pop();
            final_latencies[i] = TSC::now() - start;
        }
        responder.join();

        double freq_ghz = get_cpu_base_frequency_ghz();
        std::vector<double> ns_latencies(PING_PONGS);
        for (size_t i = 0; i < PING_PONGS; ++i) {
            ns_latencies[i] = final_latencies[i] / freq_ghz;
        }
        std::sort(ns_latencies.begin(), ns_latencies.end());

        return {
            .min_rtt_ns = ns_latencies.front(),
            .max_rtt_ns = ns_latencies.back(),
            .median_rtt_ns = ns_latencies[PING_PONGS / 2],
            .p99_rtt_ns = ns_latencies[static_cast<size_t>(PING_PONGS * 0.99)],
            .all_rtts = ns_latencies
        };
    }
};

int main() {
    TSC::init();
    auto topology = get_cpu_topology();

    using Queue = BoundedQueue<uint32_t, 8>; // 小队列，减少无关开销

    std::println("=== Ping-Pong Latency Test ===\n");
    
    auto result = PingPongBenchmark<Queue>::run(
        topology[0].hw_thread_id,
        topology[1].hw_thread_id
    );

    std::println("Round-Trip Time Statistics:");
    std::println("  Min:    {:.2f} ns", result.min_rtt_ns);
    std::println("  Median: {:.2f} ns", result.median_rtt_ns);
    std::println("  P99:    {:.2f} ns", result.p99_rtt_ns);
    std::println("  Max:    {:.2f} ns", result.max_rtt_ns);

    // 保存完整分布用于绘图
    std::ofstream csv("outputs/pingpong_latency_distribution.csv");
    csv << "RTT(ns)\n";
    for (auto rtt : result.all_rtts) {
        csv << std::format("{:.2f}\n", rtt);
    }

    return 0;
}
