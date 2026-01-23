#pragma once

#include "types.hpp"
#include <atomic>
#include <immintrin.h>

namespace shm {

// -----------------------------------------------------------------------------
// 单生产者单消费者（SPSC）无锁环形缓冲区
// -----------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
class RingBuffer {
  static_assert(is_power_of_two(Capacity), "Capacity must be power of 2");

public:
  RingBuffer() = default;

  // 禁止拷贝
  RingBuffer(const RingBuffer &) = delete;
  RingBuffer &operator=(const RingBuffer &) = delete;

  // 尝试推入数据
  bool push(const T &data) noexcept {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);

    if (tail - head >= Capacity)
      return false;

    buffer_[tail & (Capacity - 1)] = data;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  // 尝试弹出数据
  bool pop(T &out_data) noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);

    if (head == tail)
      return false;

    out_data = buffer_[head & (Capacity - 1)];
    head_.store(head + 1, std::memory_order_release);
    return true;
  }

  // 阻塞推入（自旋等待）
  void push_blocking(const T &data) noexcept {
    while (!push(data))
      _mm_pause();
  }

  // 阻塞弹出（自旋等待）
  void pop_blocking(T &out_data) noexcept {
    while (!pop(out_data))
      _mm_pause();
  }

  // 带重试次数的推入
  bool try_push(const T &data, int max_retries = 0) noexcept {
    for (int i = 0; i <= max_retries; ++i) {
      if (push(data))
        return true;
      if (i < max_retries)
        _mm_pause();
    }
    return false;
  }

  // 带重试次数的弹出
  bool try_pop(T &out_data, int max_retries = 0) noexcept {
    for (int i = 0; i <= max_retries; ++i) {
      if (pop(out_data))
        return true;
      if (i < max_retries)
        _mm_pause();
    }
    return false;
  }

  // 获取当前大小（近似值）
  size_t size() const noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_relaxed);
    return tail - head;
  }

  // 检查是否为空
  bool empty() const noexcept { return size() == 0; }

  // 检查是否已满
  bool full() const noexcept { return size() >= Capacity; }

  // 获取容量
  constexpr size_t capacity() const noexcept { return Capacity; }

private:
  alignas(config::CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
  alignas(config::CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
  alignas(config::CACHE_LINE_SIZE * 2) T buffer_[Capacity];
};

} // namespace shm
