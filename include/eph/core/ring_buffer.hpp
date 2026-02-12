#pragma once

#include "eph/platform.hpp"
#include "eph/types.hpp"
#include <array>
#include <atomic>
#include <bit>
#include <concepts>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>

namespace eph {

using namespace eph::detail;

/**
 * @brief 多缓冲顺序锁环形队列 (Buffered SeqLock RingBuffer)
 *
 * 这是一个针对 **单生产者、多消费者 (SPMC)** 场景优化的无锁数据结构。
 * 它结合了 RingBuffer 的缓冲能力与 SeqLock 的版本控制机制。
 *
 * @section 特性
 * 1. Writer Wait-free: 写入者仅需更新本地索引和原子序列号，无需自旋或阻塞。
 * 2. Reader Lock-free: 读取者通过乐观读取 (Optimistic Read) 获取数据。
 * 3. Shadow Indexing: Writer 维护本地索引副本，减少 RMW 操作。
 * 4. Cache Friendly: 核心数据结构经过严格的 Cache Line 对齐，消除伪共享。
 *
 * @tparam T 数据类型 (必须满足 TriviallyCopyable 概念)
 * @tparam Capacity 缓冲槽位数量，必须是 2 的幂。
 */
template <typename T, size_t Capacity = 8>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer {
  static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");
  static_assert(Capacity > 1, "Primary template requires Capacity > 1");

private:
  /**
   * @brief 内部存储槽位
   *
   * @note 强制对齐到 Cache Line。
   * 在 SPMC 场景下，Writer 写 slots_[i] 时，不能影响 Reader 读取 slots_[i-1]。
   */
  struct alignas(Align<T>) Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };

  // ---------------------------------------------------------------------------
  // Writer 独占区 (Hot Data)
  // ---------------------------------------------------------------------------
  struct alignas(Align<T>) WriterLine {
    /// 影子索引，仅 Writer 访问，无需原子
    uint64_t index_{0};
  } writer_;

  // ---------------------------------------------------------------------------
  // Reader 共享区 (Hot Data)
  // ---------------------------------------------------------------------------
  struct alignas(Align<T>) GlobalLine {
    /// 全局索引，Writer 写 (Release)，Reader 读 (Acquire)
    std::atomic<uint64_t> index_{0};
  } global_;

  // ---------------------------------------------------------------------------
  // 核心存储区
  // ---------------------------------------------------------------------------
  std::array<Slot, Capacity> slots_;

public:
  RingBuffer() noexcept {
    for (auto &s : slots_) {
      s.seq.store(0, std::memory_order_relaxed);
    }
    global_.index_.store(0, std::memory_order_relaxed);
    writer_.index_ = 0;
  }

  // ===========================================================================
  // Writer 操作 (Wait-free, Single Producer)
  // ===========================================================================

  /**
   * @brief 核心：零拷贝写入 (Visitor 模式)
   *
   * @tparam F 回调类型 void(T& slot)
   */
  template <typename F>
    requires std::invocable<F, T &>
  void produce(F &&writer) noexcept {
    // 1. 获取下一个写入位置 (使用本地 Shadow Index)
    const uint64_t current_idx = writer_.index_;
    const uint64_t next_idx = current_idx + 1;

    Slot &s = slots_[next_idx & (Capacity - 1)];

    // 2. 锁定槽位 (Seq 变奇数)
    uint64_t seq_old = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq_old + 1, std::memory_order_relaxed);

    // 3. Store-Store Barrier
    // 确保 seq 变为奇数的操作对其他核可见后，才开始写真正的数据
    std::atomic_thread_fence(std::memory_order_release);

    // 4. 执行数据写入/修改
    std::invoke(std::forward<F>(writer), s.data_);

    // 5. Store-Store Barrier
    // 确保数据写完后，才更新 seq 为偶数
    std::atomic_thread_fence(std::memory_order_release);

    // 6. 解锁槽位 (Seq 变偶数)
    s.seq.store(seq_old + 2, std::memory_order_relaxed);

    // 7. 发布全局索引 (Release)
    global_.index_.store(next_idx, std::memory_order_release);

    // 8. 更新本地影子索引
    writer_.index_ = next_idx;
  }

  /**
   * @brief 写入新数据 (Copy/Move)
   */
  template <typename U>
    requires std::is_assignable_v<T &, U>
  void push(U &&val) noexcept {
    produce([&](T &slot) { slot = std::forward<U>(val); });
  }

  /**
   * @brief 原地构造写入 (Emplace)
   */
  template <typename... Args>
    requires std::is_constructible_v<T, Args...>
  void emplace(Args &&...args) noexcept {
    produce([&](T &slot) {
      if constexpr (std::is_trivially_destructible_v<T>) {
        std::construct_at(&slot, std::forward<Args>(args)...);
      } else {
        std::destroy_at(&slot);
        std::construct_at(&slot, std::forward<Args>(args)...);
      }
    });
  }

  // ===========================================================================
  // Reader 操作 (Lock-free)
  // ===========================================================================

  /**
   * @brief 核心：尝试零拷贝读取 (Visitor Pattern)
   *
   * @return true 读取成功; false 数据脏 (发生竞争)
   */
  template <typename F>
    requires std::invocable<F, const T &>
  [[nodiscard]] bool try_consume_latest(F &&visitor) const noexcept {
    // 1. 获取当前最新的全局索引 (Acquire)
    uint64_t idx = global_.index_.load(std::memory_order_acquire);
    const Slot &s = slots_[idx & (Capacity - 1)];

    // 2. 读取开始前的版本号 (Acquire)
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);

    // 如果版本号为奇数，说明 Writer 正在写入
    if (seq1 & 1) [[unlikely]]
      return false;

    // 3. 执行读取
    std::invoke(std::forward<F>(visitor), s.data_);

    // 4. Load-Load Barrier
    // 强制 CPU 保证先完成上述数据的读取，再读取下方的 seq2
    std::atomic_thread_fence(std::memory_order_acquire);

    // 5. 再次读取版本号 (Relaxed)
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  /**
   * @brief 尝试读取最新数据 (值拷贝)
   */
  [[nodiscard]] bool try_pop_latest(T &out) const noexcept {
    return try_consume_latest([&out](const T &data) { out = data; });
  }

  /**
   * @brief 尝试读取并返回可选值
   */
  [[nodiscard]] std::optional<T> try_pop_latest() const noexcept {
    std::optional<T> res;
    try_consume_latest([&res](const T &data) { res.emplace(data); });
    return res;
  }

  /**
   * @brief 阻塞式零拷贝读取 (自旋直到成功)
   */
  template <typename F>
    requires std::invocable<F, const T &>
  void consume_latest(F &&visitor) const noexcept {
    while (!try_consume_latest(std::forward<F>(visitor))) {
      cpu_relax();
    }
  }

  /**
   * @brief 阻塞式值拷贝读取 (自旋直到成功)
   */
  void pop_latest(T &out) const noexcept {
    consume_latest([&out](const T &data) { out = data; });
  }

  /**
   * @brief 阻塞式读取并返回值 (自旋直到成功)
   */
  [[nodiscard]] T pop_latest() const noexcept {
    T out;
    pop_latest(out);
    return out;
  }

  // ===========================================================================
  // 状态查询 (Status Queries)
  // ===========================================================================

  /**
   * @brief 获取缓冲区容量
   */
  [[nodiscard]] static constexpr size_t capacity() noexcept { return Capacity; }

  /**
   * @brief 检查 Writer 是否正在忙碌 (写入中)
   */
  [[nodiscard]] bool busy() const noexcept {
    // 获取当前最新发布的索引
    uint64_t idx = global_.index_.load(std::memory_order_relaxed);
    // 预测 Writer 正在操作的下一个 Slot
    const Slot &s = slots_[(idx + 1) & (Capacity - 1)];
    // 检查该 Slot 的 seq 是否为奇数
    return s.seq.load(std::memory_order_relaxed) & 1;
  }
};

/**
 * @brief 三重缓冲特化版 (Triple Buffer / N=3)
 *
 * 针对 N=3 (Front-Back-Middle) 场景的优化实现。
 * 避免通用除法/取模，利用寄存器级小整数逻辑。
 */
template <typename T>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer<T, 3> {

private:
  struct alignas(Align<T>) Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };

  Slot slots_[3];

  struct alignas(Align<T>) WriterLine {
    uint8_t index_{0};
  } writer_;

  struct alignas(Align<T>) GlobalLine {
    std::atomic<uint8_t> index_{0};
  } global_;

public:
  RingBuffer() noexcept {
    for (auto &s : slots_)
      s.seq.store(0, std::memory_order_relaxed);
    global_.index_.store(0, std::memory_order_relaxed);
    writer_.index_ = 0;
  }

  static constexpr uint8_t next_slot(uint8_t current) noexcept {
    return (current + 1 == 3) ? 0 : current + 1;
  }

  // ===========================================================================
  // Writer
  // ===========================================================================

  template <typename F>
    requires std::invocable<F, T &>
  void produce(F &&writer) noexcept {
    const uint8_t next_idx = next_slot(writer_.index_);
    Slot &s = slots_[next_idx];

    // Seqlock Write: Odd -> Fence -> Write -> Fence -> Even
    uint64_t seq_old = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq_old + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    std::invoke(std::forward<F>(writer), s.data_);

    std::atomic_thread_fence(std::memory_order_release);
    s.seq.store(seq_old + 2, std::memory_order_relaxed);

    global_.index_.store(next_idx, std::memory_order_release);
    writer_.index_ = next_idx;
  }

  template <typename U>
    requires std::is_assignable_v<T &, U>
  void push(U &&val) noexcept {
    produce([&](T &slot) { slot = std::forward<U>(val); });
  }

  template <typename... Args>
    requires std::is_constructible_v<T, Args...>
  void emplace(Args &&...args) noexcept {
    produce([&](T &slot) {
      if constexpr (std::is_trivially_destructible_v<T>) {
        std::construct_at(&slot, std::forward<Args>(args)...);
      } else {
        std::destroy_at(&slot);
        std::construct_at(&slot, std::forward<Args>(args)...);
      }
    });
  }

  // ===========================================================================
  // Reader
  // ===========================================================================

  template <typename F>
    requires std::invocable<F, const T &>
  [[nodiscard]] bool try_consume_latest(F &&visitor) const noexcept {
    // 读取物理索引
    const uint8_t idx = global_.index_.load(std::memory_order_acquire);
    if (idx >= 3) [[unlikely]]
      return false;

    const Slot &s = slots_[idx];

    // Seqlock Read
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false;

    std::invoke(std::forward<F>(visitor), s.data_);

    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  [[nodiscard]] bool try_pop_latest(T &out) const noexcept {
    return try_consume_latest([&out](const T &data) { out = data; });
  }

  [[nodiscard]] std::optional<T> try_pop_latest() const noexcept {
    std::optional<T> res;
    try_consume_latest([&res](const T &data) { res.emplace(data); });
    return res;
  }

  template <typename F>
    requires std::invocable<F, const T &>
  void consume_latest(F &&visitor) const noexcept {
    while (!try_consume_latest(std::forward<F>(visitor))) {
      cpu_relax();
    }
  }

  void pop_latest(T &out) const noexcept {
    consume_latest([&out](const T &data) { out = data; });
  }

  [[nodiscard]] T pop_latest() const noexcept {
    T out;
    pop_latest(out);
    return out;
  }

  // ===========================================================================
  // 状态查询
  // ===========================================================================

  [[nodiscard]] static constexpr size_t capacity() noexcept { return 3; }

  /**
   * @brief 检查 Writer 是否忙碌
   */
  [[nodiscard]] bool busy() const noexcept {
    uint8_t idx = global_.index_.load(std::memory_order_relaxed);
    uint8_t next = next_slot(idx);
    return slots_[next].seq.load(std::memory_order_relaxed) & 1;
  }
};

/**
 * @brief 顺序锁 (SeqLock) - 单槽位特化 (N=1)
 *
 * 极度紧凑的内存布局，移除了 global_index 的维护开销。
 * 适用于数据量极小且延迟极敏感的广播场景。
 */
template <typename T>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer<T, 1> {

  static_assert(std::atomic<uint64_t>::is_always_lock_free,
                "SeqLock requires lock-free std::atomic<uint64_t>");

private:
  // 偶数=空闲，奇数=正在写入
  std::atomic<uint64_t> seq_{0};
  T data_{};

public:
  RingBuffer() noexcept = default;

  // ===========================================================================
  // Writer
  // ===========================================================================

  template <typename F>
    requires std::invocable<F, T &>
  void produce(F &&writer) noexcept {
    uint64_t seq = seq_.load(std::memory_order_relaxed);

    // 1. Lock (Seq=Odd)
    seq_.store(seq + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);

    // 2. Write
    std::invoke(std::forward<F>(writer), data_);

    // 3. Unlock (Seq=Even)
    std::atomic_thread_fence(std::memory_order_release);
    seq_.store(seq + 2, std::memory_order_relaxed);
  }

  template <typename U>
    requires std::is_assignable_v<T &, U>
  void push(U &&val) noexcept {
    produce([&](T &slot) { slot = std::forward<U>(val); });
  }

  template <typename... Args>
    requires std::is_constructible_v<T, Args...>
  void emplace(Args &&...args) noexcept {
    produce([&](T &slot) {
      if constexpr (std::is_trivially_destructible_v<T>) {
        std::construct_at(&slot, std::forward<Args>(args)...);
      } else {
        std::destroy_at(&slot);
        std::construct_at(&slot, std::forward<Args>(args)...);
      }
    });
  }

  // ===========================================================================
  // Reader
  // ===========================================================================

  template <typename F>
    requires std::invocable<F, const T &>
  [[nodiscard]] bool try_consume_latest(F &&visitor) const noexcept {
    // 1. 读取开始版本号 (Acquire)
    uint64_t seq0 = seq_.load(std::memory_order_acquire);
    if (seq0 & 1)
      return false;

    // 2. 读取数据
    std::invoke(std::forward<F>(visitor), data_);

    // 3. Load-Load Barrier
    std::atomic_thread_fence(std::memory_order_acquire);

    // 4. 验证结束版本号
    uint64_t seq1 = seq_.load(std::memory_order_relaxed);
    return seq0 == seq1;
  }

  [[nodiscard]] bool try_pop_latest(T &out) const noexcept {
    return try_consume_latest([&out](const T &slot) { out = slot; });
  }

  [[nodiscard]] std::optional<T> try_pop_latest() const noexcept {
    std::optional<T> res;
    try_consume_latest([&res](const T &slot) { res.emplace(slot); });
    return res;
  }

  template <typename F>
    requires std::invocable<F, const T &>
  void consume_latest(F &&visitor) const noexcept {
    while (!try_consume_latest(std::forward<F>(visitor))) {
      cpu_relax();
    }
  }

  void pop_latest(T &out) const noexcept {
    consume_latest([&out](const T &slot) { out = slot; });
  }

  [[nodiscard]] T pop_latest() const noexcept {
    T out;
    pop_latest(out);
    return out;
  }

  // ===========================================================================
  // 状态查询
  // ===========================================================================

  [[nodiscard]] static constexpr size_t capacity() noexcept { return 1; }

  /// 检查当前是否被写锁占用
  [[nodiscard]] bool busy() const noexcept {
    return seq_.load(std::memory_order_relaxed) & 1;
  }
};

} // namespace eph
