#include "../common.hpp"
#include "eph/benchmark/timer.hpp"
#include "seqlock_ring_buffer3.hpp"
#include <atomic>
#include <vector>

using namespace eph;
using namespace eph::benchmark;

// 确保对齐以进行公平比较
// 假设 seqlock_ring_buffer3 使用了 64 字节对齐
constexpr size_t ALIGN_SIZE = 64;

// =============================================================================
// 1. Baseline: Compile-time Modulo (% N) - Aligned
// =============================================================================
template <typename T, size_t N>
class RingBufferModuloConst {
  struct alignas(ALIGN_SIZE) Slot { // [修正] 强制对齐，与 Library 保持一致
    std::atomic<uint64_t> seq{0};
    T data_{};
  };
  Slot slots_[N];
  alignas(ALIGN_SIZE) std::atomic<uint64_t> global_index_{0};

public:
  void push(const T &val) noexcept {
    uint64_t idx = global_index_.load(std::memory_order_relaxed);
    Slot &s = slots_[(idx + 1) % N]; 
    uint64_t seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq + 1, std::memory_order_release);
    s.data_ = val;
    s.seq.store(seq + 2, std::memory_order_release);
    global_index_.store(idx + 1, std::memory_order_release);
  }
};

// =============================================================================
// 2. Contrast: Runtime Modulo (Forced DIV) - Aligned
// =============================================================================
template <typename T, size_t N>
class RingBufferModuloRuntime {
  struct alignas(ALIGN_SIZE) Slot { // [修正] 强制对齐
    std::atomic<uint64_t> seq{0};
    T data_{};
  };
  Slot slots_[N];
  alignas(ALIGN_SIZE) std::atomic<uint64_t> global_index_{0};
  
  volatile size_t capacity_ = N; 

public:
  void push(const T &val) noexcept {
    uint64_t idx = global_index_.load(std::memory_order_relaxed);
    Slot &s = slots_[(idx + 1) % capacity_];
    uint64_t seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq + 1, std::memory_order_release);
    s.data_ = val;
    s.seq.store(seq + 2, std::memory_order_release);
    global_index_.store(idx + 1, std::memory_order_release);
  }
};

// =============================================================================
// Main Benchmark
// =============================================================================
int main() {
  bind_cpu(2);
  TSC::init();

  constexpr size_t N = 1024;
  constexpr size_t OPS = 100;

  // 1. 测试 seqlock_ring_buffer3 (Native Bitwise)
  {
    auto rb = std::make_unique<eph::RingBuffer<uint64_t, N>>();
    run_bench("1. Optimized (Bitwise &)", [&]() {
      for (size_t i = 0; i < OPS; ++i) {
        rb->push(i);
      }
      do_not_optimize(*rb);
    });
  }

  // 2. 测试 Baseline (Const %) - Aligned
  {
    auto rb = std::make_unique<RingBufferModuloConst<uint64_t, N>>();
    run_bench("2. Baseline (Const %)", [&]() {
      for (size_t i = 0; i < OPS; ++i) {
        rb->push(i);
      }
      do_not_optimize(*rb);
    });
  }

  // 3. 测试 Runtime (Forced %) - Aligned
  {
    auto rb = std::make_unique<RingBufferModuloRuntime<uint64_t, N>>();
    run_bench("3. Runtime (Forced %)", [&]() {
      for (size_t i = 0; i < OPS; ++i) {
        rb->push(i);
      }
      do_not_optimize(*rb);
    });
  }

  return 0;
}
