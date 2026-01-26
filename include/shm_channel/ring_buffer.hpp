#pragma once

#include "types.hpp"
#include <atomic>
#include <optional>
#include <array>

namespace shm {

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
requires Shmable<T>
class RingBuffer {
  static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");

public:
  RingBuffer() noexcept {
     // 显式初始化原子变量
     head_.store(0, std::memory_order_relaxed);
     tail_.store(0, std::memory_order_relaxed);
  }

  // ---------------------------------------------------------------------------
  // PUSH 操作
  // ---------------------------------------------------------------------------

  bool push(const T &data) noexcept {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    // Acquire: 确保读取到消费者更新后的 head_
    const size_t head = head_.load(std::memory_order_acquire);

    if (tail - head >= Capacity) {
      return false; // Full
    }

    buffer_[tail & mask_] = data;
    
    // Release: 确保数据写入在 tail 更新之前完成
    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  void push_blocking(const T &data) noexcept {
    // 简单的自旋策略，可以优化为指数退避
    while (!push(data)) {
      cpu_relax();
    }
  }

  bool try_push(const T &data, int max_retries) noexcept {
    for (int i = 0; i <= max_retries; ++i) {
      if (push(data)) return true;
      cpu_relax();
    }
    return false;
  }

  // ---------------------------------------------------------------------------
  // POP 操作
  // ---------------------------------------------------------------------------

  std::optional<T> pop() noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    // Acquire: 确保读取到生产者更新后的 tail_
    const size_t tail = tail_.load(std::memory_order_acquire);

    if (head == tail) {
      return std::nullopt; // Empty
    }

    T data = buffer_[head & mask_];
    
    // Release: 确保数据读取在 head 更新之前完成
    head_.store(head + 1, std::memory_order_release);
    return data;
  }

  // 阻塞弹出，返回 T (值语义)
  T pop_blocking() noexcept {
    std::optional<T> data;
    while (!(data = pop())) {
      cpu_relax();
    }
    return *data;
  }

  // 避免拷贝的阻塞弹出接口
  void pop_blocking(T& out) noexcept {
    while (true) {
        // 内联逻辑以减少 optional 开销
        const size_t head = head_.load(std::memory_order_relaxed);
        const size_t tail = tail_.load(std::memory_order_acquire);
        if (head != tail) {
            out = buffer_[head & mask_];
            head_.store(head + 1, std::memory_order_release);
            return;
        }
        cpu_relax();
    }
  }

  std::optional<T> try_pop(int max_retries) noexcept {
    for (int i = 0; i <= max_retries; ++i) {
      if (auto val = pop()) return val;
      cpu_relax();
    }
    return std::nullopt;
  }

  // ---------------------------------------------------------------------------
  // 状态查询
  // ---------------------------------------------------------------------------

  size_t size() const noexcept {
    auto tail = tail_.load(std::memory_order_relaxed);
    auto head = head_.load(std::memory_order_relaxed);
    return tail - head;
  }

  bool empty() const noexcept { return size() == 0; }
  bool full() const noexcept { return size() >= Capacity; }
  static constexpr size_t capacity() noexcept { return Capacity; }

private:
  static constexpr size_t mask_ = Capacity - 1;

  // 使用 alignas 确保独立的缓存行，避免 False Sharing
  alignas(config::CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
  alignas(config::CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};
  
  // 数据区对齐
  alignas(config::CACHE_LINE_SIZE) std::array<T, Capacity> buffer_;
};

} // namespace shm
