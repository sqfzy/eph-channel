#pragma once

#include "platform.hpp"
#include "types.hpp"
#include <array>
#include <atomic>
#include <optional>

namespace eph {

/**
 * @brief 单生产者-单消费者 (SPSC) 无锁环形缓冲区
 *
 * @details
 * **核心机制：**
 * 使用 Head 和 Tail 两个索引控制读写，无需互斥锁 (Mutex)。
 * - Producer 只修改 Tail。
 * - Consumer 只修改 Head。
 *
 * **内存布局与伪共享 (False Sharing) 防护：**
 * 为了防止多核 CPU 下的缓存行颠簸 (Cache Thrashing)，Head 和 Tail 被强制隔离在不同的缓存行。
 *
 * [ head_ (8B) ... padding ... ] <--- Cache Line A (Consumer 独占写)
 * [ tail_ (8B) ... padding ... ] <--- Cache Line B (Producer 独占写)
 * [ buffer_ ...                ] <--- Cache Line C...
 *
 * @tparam T 数据类型，必须是 TriviallyCopyable (POD)。
 * @tparam Capacity 容量，必须是 2 的幂 (Power of 2)，以便使用位运算替代取模。
 */
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class RingBuffer {
  static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");
  static constexpr size_t mask_ = Capacity - 1;

  // 使用 alignas 确保独立的缓存行，避免 False Sharing
  alignas(config::CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
  alignas(config::CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};

  // 数据区对齐
  static constexpr size_t BufferAlign = (alignof(T) > config::CACHE_LINE_SIZE)
                                            ? alignof(T)
                                            : config::CACHE_LINE_SIZE;

  alignas(BufferAlign) std::array<T, Capacity> buffer_;

  // ===========================================================================
  // 内核 (Kernels) - 单次原子操作
  // ===========================================================================

  template <typename F> bool raw_produce(F &&writer) noexcept {
    const size_t tail = tail_.load(std::memory_order_relaxed);
    const size_t head = head_.load(std::memory_order_acquire);

    if (tail - head >= Capacity) {
      return false; // Full
    }

    // 完美转发 writer，直接在 SHM 内存上操作
    std::forward<F>(writer)(buffer_[tail & mask_]);

    tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  template <typename F> bool raw_consume(F &&visitor) noexcept {
    const size_t head = head_.load(std::memory_order_relaxed);
    const size_t tail = tail_.load(std::memory_order_acquire);

    if (head == tail) {
      return false; // Empty
    }

    std::forward<F>(visitor)(buffer_[head & mask_]);

    head_.store(head + 1, std::memory_order_release);
    return true;
  }

public:
  RingBuffer() noexcept {
    head_.store(0, std::memory_order_relaxed);
    tail_.store(0, std::memory_order_relaxed);
  }

  // ===========================================================================
  // PUSH 操作 (非阻塞 / 阻塞)
  // ===========================================================================

  // PERF: 尝试零拷贝写入
  template <typename F> bool try_produce(F &&writer) noexcept {
    return raw_produce(std::forward<F>(writer));
  }

  // PERF: 尝试 T 拷贝赋值
  bool try_push(const T &data) noexcept {
    return raw_produce([&data](T &slot) { slot = data; });
  }

  // PERF: 尝试 T 移动赋值
  bool try_push(T &&data) noexcept {
    return raw_produce([&data](T &slot) { slot = std::move(data); });
  }

  // PERF: 阻塞式零拷贝写入
  template <typename F> void produce(F &&writer) noexcept {
    while (!raw_produce(writer)) {
      cpu_relax();
    }
  }

  // PERF: 阻塞式 T 拷贝赋值
  void push(const T &data) noexcept {
    produce([&](T &slot) { slot = data; });
  }

  // PERF: 阻塞式 T 移动赋值
  void push(T &&data) noexcept {
    produce([&](T &slot) { slot = std::move(data); });
  }

  // ===========================================================================
  // POP 操作 (非阻塞 / 阻塞)
  // ===========================================================================

  // PERF: 尝试零拷贝读取
  template <typename F> bool try_consume(F &&visitor) noexcept {
    return raw_consume(std::forward<F>(visitor));
  }

  // PERF: 尝试 T 拷贝赋值 (复用内存)
  bool try_pop(T &out) noexcept {
    return raw_consume([&out](const T &data) { out = data; });
  }

  // PERF: 尝试 T 拷贝构造 (寄存器返回)
  std::optional<T> try_pop() noexcept {
    std::optional<T> res;
    if (raw_consume([&res](const T &data) { res.emplace(data); })) {
      return res;
    }
    return std::nullopt;
  }

  // PERF: 阻塞式零拷贝读取
  template <typename F> void consume(F &&visitor) noexcept {
    while (!raw_consume(visitor)) {
      cpu_relax();
    }
  }

  // PERF: 阻塞式 T 拷贝赋值
  void pop(T &out) noexcept {
    consume([&out](const T &data) { out = data; });
  }

  // PERF: 阻塞式 T 值返回
  T pop() noexcept {
    std::optional<T> res;
    consume([&res](const T &data) { res.emplace(data); });
    return *res;
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
};

} // namespace shm
