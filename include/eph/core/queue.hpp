#pragma once

#include "eph/platform.hpp"
#include "eph/types.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <optional>

namespace eph {

/**
 * @brief 单生产者-单消费者 (SPSC) 无锁环形缓冲区
 *
 * 这是一个针对单对单通信优化的无锁队列，采用了影子索引 (Shadow Indices)
 * 机制以最大限度减少缓存一致性流量。
 *
 * @section 特性
 * 1. Shadow Indexing: 生产者维护本地 `shadow_head_`，消费者维护本地
 * `shadow_tail_`， 仅在缓冲区状态看似“满”或“空”时才同步全局原子索引。
 * 2. **Cache Friendly**: 核心数据结构经过严格的 Cache Line 对齐，消除伪共享
 * (False Sharing)。
 *
 * @tparam T 数据类型，必须满足 TriviallyCopyable 概念。
 * @tparam Capacity 缓冲区容量，必须是 2 的幂。
 */
template <typename T, size_t Capacity>
  requires ShmData<T>
class BoundedQueue {
  static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");
  static constexpr size_t mask_ = Capacity - 1;

private:
  // ---------------------------------------------------------------------------
  // 消费者热点区 (Consumer Hot Data)
  // ---------------------------------------------------------------------------
  struct alignas(Align<T>) ConsumerLine {
    /// 全局读取索引 (Head Pointer)
    std::atomic<size_t> head_{0};
    /// 本地影子写入索引：记录上一次看到的 tail_，减少对 ProducerLine 的跨核访问
    size_t shadow_tail_{0};
  };

  // ---------------------------------------------------------------------------
  // 生产者热点区 (Producer Hot Data)
  // ---------------------------------------------------------------------------
  struct alignas(Align<T>) ProducerLine {
    /// 全局写入索引 (Tail Pointer)
    std::atomic<size_t> tail_{0};
    /// 本地影子读取索引：记录上一次看到的 head_，减少对 ConsumerLine 的跨核访问
    size_t shadow_head_{0};
  };

  ConsumerLine consumer_;
  ProducerLine producer_;

  /// 核心数据存储区
  alignas(Align<T>) std::array<T, Capacity> buffer_;

public:
  BoundedQueue() noexcept {
    consumer_.head_.store(0, std::memory_order_relaxed);
    producer_.tail_.store(0, std::memory_order_relaxed);
    consumer_.shadow_tail_ = 0;
    producer_.shadow_head_ = 0;
  }

  // ===========================================================================
  // PUSH 操作 (Producer Operations)
  // ===========================================================================

  /**
   * @brief 生产逻辑内核
   */
  template <typename F> bool raw_produce(F &&writer) noexcept {
    // 1. 获取本地写入索引 (Relaxed)
    const size_t tail = producer_.tail_.load(std::memory_order_relaxed);

    // 2. 快速路径：检查影子索引空间
    // shadow_head_ 是 head_ 的历史快照，一定 <= 实际 head_。
    if (tail - producer_.shadow_head_ >= Capacity) {

      // 3. 慢速路径：影子索引认为已满，尝试从内存加载最新的全局 head_ (Acquire)
      const size_t head = consumer_.head_.load(std::memory_order_acquire);
      producer_.shadow_head_ = head; // 更新本地缓存

      // 4. 再次检查真实容量状态
      if (tail - head >= Capacity) {
        return false; // Full
      }
    }

    // 5. 执行数据写入 (通常是原地构造或赋值)
    std::forward<F>(writer)(buffer_[tail & mask_]);

    // 6. 发布新的写入索引 (Release)
    producer_.tail_.store(tail + 1, std::memory_order_release);
    return true;
  }

  /**
   * @brief 尝试零拷贝写入
   * @param writer 回调函数 void(T& slot)
   * @return true 成功; false 队列已满
   */
  template <typename F> bool try_produce(F &&writer) noexcept {
    return raw_produce(std::forward<F>(writer));
  }

  /**
   * @brief 尝试拷贝数据到队列
   * @param data 源数据
   * @return true 成功; false 队列已满
   */
  bool try_push(const T &data) noexcept {
    return raw_produce([&data](T &slot) { slot = data; });
  }

  /**
   * @brief 尝试移动数据到队列
   * @param data 源数据
   * @return true 成功; false 队列已满
   */
  bool try_push(T &&data) noexcept {
    return raw_produce([&data](T &slot) { slot = std::move(data); });
  }

  /**
   * @brief 阻塞式零拷贝写入 (自旋直到有空间)
   */
  template <typename F> void produce(F &&writer) noexcept {
    while (!raw_produce(writer)) {
      cpu_relax();
    }
  }

  /**
   * @brief 阻塞式拷贝数据到队列
   */
  void push(const T &data) noexcept {
    produce([&](T &slot) { slot = data; });
  }

  /**
   * @brief 阻塞式移动数据到队列
   */
  void push(T &&data) noexcept {
    produce([&](T &slot) { slot = std::move(data); });
  }

  // ===========================================================================
  // POP 操作 (Consumer Operations)
  // ===========================================================================

  /**
   * @brief 消费逻辑内核
   */
  template <typename F> bool raw_consume(F &&visitor) noexcept {
    // 1. 获取本地读取索引 (Relaxed)
    const size_t head = consumer_.head_.load(std::memory_order_relaxed);

    // 2. 快速路径：使用影子索引检查是否有数据
    // shadow_tail_ 是 tail_ 的历史快照，一定 <= 实际 tail_。
    if (consumer_.shadow_tail_ == head) {

      // 3. 慢速路径：影子索引认为已空，重新加载最新的全局 tail_ (Acquire)
      const size_t tail = producer_.tail_.load(std::memory_order_acquire);
      consumer_.shadow_tail_ = tail; // 更新本地缓存

      // 4. 再次检查真实状态
      if (head == tail) {
        return false; // Empty
      }
    }

    // 5. 访问缓冲区数据
    std::forward<F>(visitor)(buffer_[head & mask_]);

    // 6. 发布新的读取索引 (Release)
    consumer_.head_.store(head + 1, std::memory_order_release);
    return true;
  }

  /**
   * @brief 尝试零拷贝读取
   * @param visitor 回调函数 void(const T& data)
   * @return true 成功; false 队列为空
   */
  template <typename F> bool try_consume(F &&visitor) noexcept {
    return raw_consume(std::forward<F>(visitor));
  }

  /**
   * @brief 尝试读取并拷贝数据
   * @param out [out] 目标对象
   * @return true 成功; false 队列为空
   */
  bool try_pop(T &out) noexcept {
    return raw_consume([&out](const T &data) { out = data; });
  }

  /**
   * @brief 尝试读取并返回可选值
   * @return std::optional 包含数据或空
   */
  std::optional<T> try_pop() noexcept {
    std::optional<T> res;
    if (raw_consume([&res](const T &data) { res.emplace(data); })) {
      return res;
    }
    return std::nullopt;
  }

  /**
   * @brief 阻塞式零拷贝读取
   */
  template <typename F> void consume(F &&visitor) noexcept {
    while (!raw_consume(visitor)) {
      cpu_relax();
    }
  }

  /**
   * @brief 阻塞式拷贝读取
   */
  void pop(T &out) noexcept {
    consume([&out](const T &data) { out = data; });
  }

  /**
   * @brief 阻塞式读取并返回值
   */
  T pop() noexcept {
    std::optional<T> res;
    consume([&res](const T &data) { res.emplace(data); });
    return *res;
  }

  // ===========================================================================
  // 状态查询 (Status Queries)
  // ===========================================================================

  /// 获取当前队列中的元素数量 (估计值)
  size_t size() const noexcept {
    auto tail = producer_.tail_.load(std::memory_order_relaxed);
    auto head = consumer_.head_.load(std::memory_order_relaxed);
    return tail - head;
  }

  /// 检查队列是否为空
  bool empty() const noexcept { return size() == 0; }

  /// 检查队列是否已满
  bool full() const noexcept { return size() >= Capacity; }

  /// 获取队列固定容量
  static constexpr size_t capacity() noexcept { return Capacity; }
};

} // namespace eph
