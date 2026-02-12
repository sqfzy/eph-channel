#include <benchmark/benchmark.h>
#include <array>
#include <cstddef>
#include <cstring>
#include <vector>
#include <eph/benchmark/timer.hpp>
#include <eph/platform.hpp>
#include "eph/core/queue.hpp"

// ---------------------------------------------------------------------------
// 辅助函数与 Mock
// ---------------------------------------------------------------------------
enum class MsgType : uint8_t { Text = 1, Binary = 2 };
constexpr uint8_t FLAG_PROBE = 0x01;

// 简单的 TSC 读取
static inline uint64_t rdtsc() {
#if defined(__x86_64__) || defined(__i386__)
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
#else
    return 0; 
#endif
}

class QueueFixture : public benchmark::Fixture {
public:
    using QueueType = eph::BoundedQueue<std::array<std::byte, 2048>, 4096>;
    std::unique_ptr<QueueType> txq2_;

    void SetUp(const ::benchmark::State& state) override {
        if (state.thread_index() == 0) {
             txq2_ = std::make_unique<QueueType>();
        }
    }

    void TearDown(const ::benchmark::State& state) override {
        if (state.thread_index() == 0) {
            txq2_.reset();
        }
    }
};

BENCHMARK_DEFINE_F(QueueFixture, BM_SPSC_Latency)(benchmark::State& state) {
    const uint32_t json_len = state.range(0);
    const std::vector<uint8_t> dummy_json(json_len, 0x42);
    
    // 确保队列指针在多线程间可见（简单起见，实际场景可能需要原子指针或 barrier）
    // 在 GBench 中，SetUp 在主线程运行，这里直接用即可。

    if (state.thread_index() == 0) {
        // --- 生产者线程 ---
        for ([[maybe_unused]] auto _ : state) {
            // 核心修改：死循环等待直到生产成功，不调用 state.KeepRunning()
            bool success = false;
            while (!success) {
                success = txq2_->try_produce([&](std::array<std::byte, 2048>& slot) {
                    // 1. 填充 Header
                    slot[0] = static_cast<std::byte>(MsgType::Text);
                    slot[1] = static_cast<std::byte>(0);
                    uint16_t total_payload_len = static_cast<uint16_t>(sizeof(uint64_t) + json_len);
                    std::memcpy(slot.data() + 2, &total_payload_len, sizeof(total_payload_len));

                    // 2. 注入 TSC
                    uint64_t tsc = rdtsc();
                    // Offset 4: Type(1)+Flags(1)+Len(2)
                    std::memcpy(slot.data() + 4, &tsc, sizeof(uint64_t)); 

                    // 3. 填充 Payload
                    if (json_len > 0) {
                        std::memcpy(slot.data() + 4 + sizeof(uint64_t), dummy_json.data(), json_len);
                    }
                });

                if (!success) {
                    eph::cpu_relax(); // 队列满，让出流水线
                }
            }
        }
    } else {
        // --- 消费者线程 ---
        // 初始化计数器，使用 kAvgIterations 自动计算平均值
        state.counters["Latency_TSC"] = benchmark::Counter(0, benchmark::Counter::kAvgIterations);

        for ([[maybe_unused]] auto _ : state) {
            bool consumed = false;
            // 核心修改：阻塞等待直到消费成功
            // 注意：使用 eph::BoundedQueue 自带的 consume 也可以，这里展开写是为了演示原理
            while (!consumed) {
                consumed = txq2_->try_consume([&](std::array<std::byte, 2048>& slot) {
                    // 1. 解析
                    const uint8_t* payload_ptr = reinterpret_cast<const uint8_t*>(slot.data()) + 4;
                    
                    uint64_t t0;
                    std::memcpy(&t0, payload_ptr, sizeof(uint64_t));
                    
                    uint64_t t1 = rdtsc();

                    // 2. 统计 (使用 += 累加)
                    // 注意防止 t1 < t0 的情况（跨核时钟偏差或乱序）
                    if (t1 >= t0) {
                        state.counters["Latency_TSC"] += static_cast<double>(t1 - t0);
                    }
                    
                    // 模拟防止优化
                    benchmark::DoNotOptimize(slot);
                });

                if (!consumed) {
                    eph::cpu_relax(); // 队列空，让出流水线
                }
            }
        }
    }

    // 设置吞吐量统计
    state.SetBytesProcessed(state.iterations() * (json_len + 12)); // 12 = Header overhead
}

BENCHMARK_REGISTER_F(QueueFixture, BM_SPSC_Latency)
    ->Threads(2) // 强制 2 个线程
    ->Arg(64)
    ->Arg(512)
    ->Arg(1024)
    ->UseRealTime(); // 推荐使用 RealTime 统计墙上时间

BENCHMARK_MAIN();
