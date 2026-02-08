#pragma once

#include "eph/platform.hpp"
#include "eph/types.hpp"
#include <atomic>
#include <cassert>
#include <cstring>
#include <utility>

namespace eph {

using namespace eph::detail;

/**
 * @brief 环形缓冲顺序锁 (Baseline Version)
 *
 * 这是一个去除了 Shadow Index 和取模优化的基准版本。
 * 用于测试和对比优化效果。
 *
 * @section Baseline 特征
 * 1. Writer 需读取共享原子变量 `global_index_`。
 * 2. 使用 `% N` 进行取模，允许任意 N > 1。
 * 3. 移除了防伪共享的 padding。
 *
 * @tparam T 数据类型
 * @tparam N 缓冲槽位数量
 */
template <typename T, size_t N = 8>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer {
  static_assert(N > 1, "RingBuffer requires N > 1");

private:
  struct alignas(Align<T>) Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };

  // ---------------------------------------------------------------------------
  // 核心存储区
  // ---------------------------------------------------------------------------

  Slot slots_[N];

  // ---------------------------------------------------------------------------
  // 共享索引区
  // ---------------------------------------------------------------------------

  // 全局单调递增索引，指向“最新已完成写入”的槽位
  // Baseline: Writer 和 Reader 都会频繁访问此变量
  std::atomic<uint64_t> global_index_{0};

public:
  RingBuffer() noexcept {
    for (auto &s : slots_) {
      s.seq.store(0, std::memory_order_relaxed);
    }
    global_index_.store(0, std::memory_order_relaxed);
  }

  // ===========================================================================
  // Writer 操作 (Wait-free)
  // ===========================================================================

  void push(const T &val) noexcept {
    // 1. 获取当前索引 (Baseline: 必须读取共享原子变量)
    // 虽然是 Relaxed，但这会导致该 Cache Line 被频繁加载
    uint64_t current_idx = global_index_.load(std::memory_order_relaxed);
    uint64_t next_idx = current_idx + 1;

    // 2. 取模计算物理位置 (Baseline: 使用通用的除法取模)
    Slot &s = slots_[next_idx % N];

    // 3. 锁定槽位
    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);

    // 4. 数据拷贝
    s.data_ = val;

    // 5. 解锁槽位
    s.seq.store(start_seq + 2, std::memory_order_release);

    // 6. 发布索引
    global_index_.store(next_idx, std::memory_order_release);
  }

  template <typename F> void write(F &&writer) noexcept {
    uint64_t current_idx = global_index_.load(std::memory_order_relaxed);
    uint64_t next_idx = current_idx + 1;

    Slot &s = slots_[next_idx % N];

    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);

    std::forward<F>(writer)(s.data_);

    s.seq.store(start_seq + 2, std::memory_order_release);
    global_index_.store(next_idx, std::memory_order_release);
  }

  // ===========================================================================
  // Reader 操作 (Lock-free)
  // ===========================================================================

  template <typename F> bool try_read(F &&visitor) const noexcept {
    // 1. 获取最新索引
    uint64_t idx = global_index_.load(std::memory_order_acquire);

    // Baseline: 使用通用取模
    const Slot &s = slots_[idx % N];

    // 2. 乐观读取
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false;

    std::forward<F>(visitor)(s.data_);

    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  bool try_pop(T &out) const noexcept {
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    const Slot &s = slots_[idx % N];

    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false;

    out = s.data_;

    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  T pop() const noexcept {
    T out;
    while (!try_pop(out)) {
      cpu_relax();
    }
    return out;
  }

  template <typename F> void read(F &&visitor) const noexcept {
    while (!try_read(visitor)) {
      cpu_relax();
    }
  }
};
} // namespace eph
