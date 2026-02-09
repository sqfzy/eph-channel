#pragma once

#include "eph/benchmark/cpu_topology.hpp"
#include "eph/benchmark/timer.hpp"
#include <thread>
#include <barrier>
#include <array>

namespace eph::benchmark {

template<typename Queue>
struct LatencyResult {
    double round_trip_ns;
    std::array<uint64_t, 10> run_cycles; // 保存每轮的 TSC cycles
};

template<typename Queue>
class LatencyBenchmark {
    static constexpr size_t MESSAGES = 100'000;
    static constexpr size_t RUNS = 10;

public:
    static LatencyResult<Queue> run_ping_pong(
        unsigned cpu1, unsigned cpu2
    ) {
        LatencyResult<Queue> result{};

        for (size_t run = 0; run < RUNS; ++run) {
            Queue q1, q2;
            std::barrier<> sync(2);
            uint64_t receiver_cycles = 0;

            std::thread receiver([&] {
                set_thread_affinity(cpu2);
                sync.arrive_and_wait();

                auto start = TSC::now();
                for (size_t i = 0; i < MESSAGES; ++i) {
                    uint32_t val = q1.pop();
                    q2.push(val);
                }
                receiver_cycles = TSC::now() - start;
            });

            // 发送者（主线程）
            set_thread_affinity(cpu1);
            sync.arrive_and_wait();

            auto start = TSC::now();
            for (size_t i = 0; i < MESSAGES; ++i) {
                q1.push(static_cast<uint32_t>(i));
                uint32_t reply = q2.pop();
                do_not_optimize(reply);
            }
            uint64_t sender_cycles = TSC::now() - start;

            receiver.join();

            result.run_cycles[run] = (sender_cycles + receiver_cycles) / 2;
        }

        // 选择最小值（最佳性能）
        auto best_cycles = *std::min_element(result.run_cycles.begin(), result.run_cycles.end());
        double freq_ghz = get_cpu_base_frequency_ghz();
        result.round_trip_ns = (best_cycles / static_cast<double>(MESSAGES)) / freq_ghz;

        return result;
    }
};

} // namespace eph::benchmark
