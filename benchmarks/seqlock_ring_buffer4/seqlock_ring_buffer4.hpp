#pragma once

#include "eph/platform.hpp"
#include "eph/types.hpp"
#include <atomic>

namespace eph {

using namespace eph::detail;

template <typename T, size_t N = 8>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer {
  static_assert(N > 1, "N must be > 1");

private:
  struct alignas(Align<T>) Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };

  Slot slots_[N];

  // [新增] 影子索引：Writer 独占
  uint64_t writer_index_{0};

  // 共享索引
  std::atomic<uint64_t> global_index_{0};

public:
  RingBuffer() noexcept {
    for (auto &s : slots_)
      s.seq.store(0, std::memory_order_relaxed);
    global_index_.store(0, std::memory_order_relaxed);
    writer_index_ = 0;
  }

  void push(const T &val) noexcept {
    // [优化] 读本地变量
    const uint64_t current_idx = writer_index_;
    const uint64_t next_idx = current_idx + 1;

    // [未优化]
    Slot &s = slots_[next_idx % N];

    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);

    s.data_ = val;

    s.seq.store(start_seq + 2, std::memory_order_release);
    global_index_.store(next_idx, std::memory_order_release);

    // [新增] 更新本地变量
    writer_index_ = next_idx;
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
    while (!try_pop(out))
      cpu_relax();
    return out;
  }
};

} // namespace eph
