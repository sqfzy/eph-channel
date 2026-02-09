#include "common.hpp"
#include "eph/benchmark/timer.hpp"
#include <atomic>
#include <memory>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

constexpr size_t CACHE_LINE = 64;
constexpr size_t DataSize = 80 - 8;

// =============================================================================
// 1. Padded: 强制对齐 (牺牲空间换取隔离)
// =============================================================================
template <typename T, size_t N> class RingBufferPadded {
  // [关键] 强制每个 Slot 独占 Cache Line
  struct alignas(CACHE_LINE) Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };
  Slot slots_[N];

  // Index 也需要隔离
  alignas(CACHE_LINE) std::atomic<uint64_t> global_index_{0};

public:
  void push(const T &val) noexcept {
    // 简单的 Writer 逻辑：一直往前写
    uint64_t idx = global_index_.load(std::memory_order_relaxed);
    Slot &s = slots_[idx & (N - 1)];

    uint64_t seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq + 1, std::memory_order_release);
    s.data_ = val;
    s.seq.store(seq + 2, std::memory_order_release);

    global_index_.store(idx + 1, std::memory_order_release);
  }

  // Reader 尝试读取最新数据
  bool try_read_latest(T &out) const noexcept {
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    if (idx == 0)
      return false;

    // 读取最新写入的一个 (idx - 1)
    const Slot &s = slots_[(idx - 1) & (N - 1)];

    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false; // 正在写

    out = s.data_;
    std::atomic_thread_fence(std::memory_order_acquire);

    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);
    return seq1 == seq2;
  }
};

// =============================================================================
// 2. Packed: 紧凑布局 (牺牲隔离换取空间)
// =============================================================================
template <typename T, size_t N> class RingBufferPacked {
  // [关键] 无对齐，16B 一个 Slot，4个挤在一个 Cache Line
  struct Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };
  Slot slots_[N];

  alignas(CACHE_LINE) std::atomic<uint64_t> global_index_{0};

public:
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
    if (idx == 0)
      return false;

    const Slot &s = slots_[(idx - 1) & (N - 1)];

    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false;

    out = s.data_;
    std::atomic_thread_fence(std::memory_order_acquire);

    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);
    return seq1 == seq2;
  }
};

// =============================================================================
// Benchmark Engine
// =============================================================================
template <typename RingBufferType>
void run_spsc_bench(const char *name, size_t n_ops) {
  auto rb = std::make_unique<RingBufferType>();
  std::atomic<bool> start{false};

  // 1. Reader 线程 (干扰源)
  std::jthread reader([&](std::stop_token st) {
    bind_cpu(3); // 绑定到 Core 3
    while (!start.load(std::memory_order_relaxed)) {
      std::this_thread::yield();
    }

    MockData<DataSize> val;
    while (!st.stop_requested()) {
      // 疯狂读取最新数据，试图制造 Cache Line 竞争
      if (rb->try_read_latest(val)) {
      }
      cpu_relax();
    }
  });

  // 2. Writer 线程 (被测对象)
  auto writer_func = [&]() {
    bind_cpu(2); // 绑定到 Core 2
    start.store(true);
    MockData<DataSize> val;

    for (size_t i = 0; i < n_ops; ++i) {
      rb->push(val);
    }
  };

  // 运行并计时
  run_bench(name, writer_func, {.limit = load_limit()});
}

int main() {
  TSC::init();
  constexpr size_t N = 1024;
  constexpr size_t OPS = 1000;

  // 1. 测试 Padded (无伪共享)
  run_spsc_bench<RingBufferPadded<MockData<DataSize>, N>>(
      "1. Padded (alignas 64)", OPS);

  // 2. 测试 Packed (高伪共享风险)
  run_spsc_bench<RingBufferPacked<MockData<DataSize>, N>>(
      "2. Packed (No alignas)", OPS);

  return 0;
}
