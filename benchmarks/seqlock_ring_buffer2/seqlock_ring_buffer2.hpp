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

  // [新增] 强制 Cache Line 隔离
  alignas(Align<T>) std::atomic<uint64_t> global_index_{0};
  char pad_tail_[Align<T> - sizeof(std::atomic<uint64_t>)];

public:
  RingBuffer() noexcept {
    for (auto &s : slots_) s.seq.store(0, std::memory_order_relaxed);
    global_index_.store(0, std::memory_order_relaxed);
  }

  void push(const T &val) noexcept {
    // [未优化]
    uint64_t current_idx = global_index_.load(std::memory_order_relaxed);
    uint64_t next_idx = current_idx + 1;

    // [未优化]
    Slot &s = slots_[next_idx % N];

    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);
    s.data_ = val;
    s.seq.store(start_seq + 2, std::memory_order_release);
    global_index_.store(next_idx, std::memory_order_release);
  }

  bool try_pop(T &out) const noexcept {
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    const Slot &s = slots_[idx % N];
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1) return false;
    out = s.data_;
    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);
    return seq1 == seq2;
  }

  T pop() const noexcept {
    T out;
    while (!try_pop(out)) cpu_relax();
    return out;
  }

  // N=1 特化与 Baseline 一致
};

template <typename T>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer<T, 1> {
  // 实现与 Baseline 一致
private:
  std::atomic<uint64_t> seq_{0};
  T data_{};
public:
  void push(const T &val) noexcept {
    uint64_t seq = seq_.load(std::memory_order_relaxed);
    seq_.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    data_ = val;
    std::atomic_thread_fence(std::memory_order_release);
    seq_.store(seq + 2, std::memory_order_relaxed);
  }
  bool try_pop(T &out) const noexcept {
    uint64_t s0 = seq_.load(std::memory_order_relaxed);
    if (s0 & 1) return false;
    std::atomic_thread_fence(std::memory_order_acquire);
    out = data_;
    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t s1 = seq_.load(std::memory_order_relaxed);
    return s0 == s1;
  }
  T pop() const noexcept {
    T out;
    while(!try_pop(out)) cpu_relax();
    return out;
  }
};

} // namespace eph
