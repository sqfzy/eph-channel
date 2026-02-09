#include "common.hpp"
#include "eph/benchmark/timer.hpp"
#include <atomic>
#include <algorithm>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

using namespace eph;
using namespace eph::benchmark;

template <typename T, size_t N> struct RingBufferAligned {
    // 强制 Slot 占用 64 字节对齐，彻底消除 Slot 间的伪共享
    struct alignas(64) Slot {
        std::atomic<uint64_t> seq{0};
        T data_{};
    };

    // 索引与数据区间也要隔离
    alignas(64) std::atomic<uint64_t> global_index_{0};
    char _padding[64]; 
    Slot slots_[N];

    void push(const T &val) noexcept {
        uint64_t idx = global_index_.load(std::memory_order_relaxed);
        Slot &s = slots_[idx & (N - 1)];
        uint64_t seq = s.seq.load(std::memory_order_relaxed);
        s.seq.store(seq + 1, std::memory_order_release);
        s.data_ = val;
        s.seq.store(seq + 2, std::memory_order_release);
        global_index_.store(idx + 1, std::memory_order_release);
    }

    bool try_read_latest(T &out) const noexcept {
        uint64_t idx = global_index_.load(std::memory_order_acquire);
        if (idx == 0) return false;
        const Slot &s = slots_[(idx - 1) & (N - 1)];
        uint64_t seq1 = s.seq.load(std::memory_order_acquire);
        if (seq1 & 1) return false;
        out = s.data_;
        std::atomic_thread_fence(std::memory_order_acquire);
        uint64_t seq2 = s.seq.load(std::memory_order_relaxed);
        return seq1 == seq2;
    }
};

template <size_t TotalSize> struct Payload {
    uint64_t tsc;
    char padding[TotalSize > sizeof(uint64_t) ? TotalSize - sizeof(uint64_t) : 0];
};

template <size_t DataSize> void run_datasize_test(const char *label) {
    const size_t N = 16;
    using T = Payload<DataSize>;
    auto rb = std::make_unique<RingBufferAligned<T, N>>();

    std::atomic<bool> running{true};
    std::atomic<bool> start{false};
    
    // 预留空间存储延迟数据，避免动态扩容干扰
    std::vector<uint64_t> lats;
    lats.reserve(10'000'000); 

    {
        std::jthread reader([&](std::stop_token st) {
            bind_cpu(3);
            while (!start.load(std::memory_order_relaxed));
            T out;
            while (!st.stop_requested() && running.load(std::memory_order_relaxed)) {
                if (rb->try_read_latest(out)) {
                    uint64_t now = TSC::now();
                    if (now >= out.tsc) {
                        lats.push_back(now - out.tsc);
                    }
                }
            }
        });

        std::jthread writer([&](std::stop_token st) {
            bind_cpu(2);
            // 预热 100ms 让频率升上去
            auto warm_up = TSC::now() + TSC::to_cycles(100'000'000);
            while(TSC::now() < warm_up);
            
            start.store(true);
            while (!st.stop_requested() && running.load(std::memory_order_relaxed)) {
                T val; val.tsc = TSC::now();
                rb->push(val);
            }
        });

        std::this_thread::sleep_for(std::chrono::seconds(2));
        running.store(false);
    }

    if (lats.empty()) return;

    // 统计分布
    std::sort(lats.begin(), lats.end());
    auto p50 = lats[lats.size() * 0.5];
    auto p99 = lats[lats.size() * 0.99];
    double avg = std::accumulate(lats.begin(), lats.end(), 0.0) / lats.size();

    std::print("{:<20} | P50: {:>4.0f} ns | P99: {:>5.0f} ns | Avg: {:>5.0f} ns | Samples: {}\n", 
               label, TSC::to_ns(p50), TSC::to_ns(p99), TSC::to_ns(avg), lats.size());
}

int main() {
    TSC::init();
    std::print("Test: Impact of DataSize on Latency (Fixed N=16)\n");
    std::print("--------------------------------------------------------------------------------\n");

    run_datasize_test<8>("Small (1x u64)");
    run_datasize_test<32>("Half CacheLine");
    run_datasize_test<56>("Full Slot (64B)"); // 8B seq + 56B data = 64B
    run_datasize_test<64>("Cross Line (64B+8B)"); 
    run_datasize_test<128>("Two CacheLines");
    run_datasize_test<256>("Large Payload");

    return 0;
}
