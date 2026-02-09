#pragma once

#include "eph/benchmark/cpu_topology.hpp"
#include "eph/platform.hpp"
#include <atomic>
#include <cmath>
#include <numeric>
#include <thread>
#include <vector>
#include <barrier>

namespace eph::benchmark {

template<typename Queue>
struct ThroughputResult {
    size_t total_messages;
    double elapsed_seconds;
    double throughput_msg_per_sec;
    std::vector<double> runs; // 多轮结果
};

template<typename Queue>
class ThroughputBenchmark {
    static constexpr size_t TOTAL_MESSAGES = 1'000'000;
    static constexpr size_t RUNS = 33;

public:
    // MPMC 吞吐量测试
    static ThroughputResult<Queue> run_mpmc(
        size_t num_producers, size_t num_consumers,
        const std::vector<CpuTopologyInfo>& topology
    ) {
        ThroughputResult<Queue> result{.total_messages = TOTAL_MESSAGES};
        std::vector<double> run_times;

        for (size_t run = 0; run < RUNS; ++run) {
            Queue queue;
            std::atomic<size_t> produced{0};
            std::atomic<size_t> consumed{0};
            std::atomic<bool> stop{false};
            std::barrier sync(num_producers + num_consumers + 1);

            std::vector<std::jthread> threads;

            // 生产者线程
            for (size_t i = 0; i < num_producers; ++i) {
                threads.emplace_back([&, cpu_id = topology[i].hw_thread_id] {
                    set_thread_affinity(cpu_id);
                    sync.arrive_and_wait(); // 等待所有线程就绪

                    size_t local_count = 0;
                    while (true) {
                        size_t current = produced.fetch_add(1, std::memory_order_relaxed);
                        if (current >= TOTAL_MESSAGES) break;

                        while (!queue.try_push(static_cast<uint32_t>(current))) {
                            cpu_relax();
                        }
                        ++local_count;
                    }
                });
            }

            // 消费者线程
            for (size_t i = 0; i < num_consumers; ++i) {
                threads.emplace_back([&, cpu_id = topology[num_producers + i].hw_thread_id] {
                    set_thread_affinity(cpu_id);
                    sync.arrive_and_wait();

                    uint32_t value;
                    while (consumed.load(std::memory_order_relaxed) < TOTAL_MESSAGES) {
                        if (queue.try_pop(value)) {
                            consumed.fetch_add(1, std::memory_order_relaxed);
                        } else {
                            cpu_relax();
                        }
                    }
                });
            }

            // 主线程开始计时
            sync.arrive_and_wait();
            auto start = std::chrono::steady_clock::now();

            // 等待所有线程完成
            threads.clear();

            auto end = std::chrono::steady_clock::now();
            double elapsed = std::chrono::duration<double>(end - start).count();
            run_times.push_back(elapsed);
        }

        // 统计：取 min, max, mean, stdev
        auto [min, max, mean, stdev] = compute_stats(run_times);
        result.elapsed_seconds = mean;
        result.throughput_msg_per_sec = TOTAL_MESSAGES / mean;
        result.runs = run_times;

        return result;
    }

private:
    static std::tuple<double, double, double, double> compute_stats(const std::vector<double>& data) {
        double min = *std::min_element(data.begin(), data.end());
        double max = *std::max_element(data.begin(), data.end());
        double sum = std::accumulate(data.begin(), data.end(), 0.0);
        double mean = sum / data.size();

        double sq_sum = 0.0;
        for (auto v : data) sq_sum += (v - mean) * (v - mean);
        double stdev = std::sqrt(sq_sum / data.size());

        return {min, max, mean, stdev};
    }
};

} // namespace eph::benchmark
