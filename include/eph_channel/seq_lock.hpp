#pragma once

#include "platform.hpp"
#include "types.hpp"
#include <atomic>

namespace eph {

/**
 * @brief 单生产者-多消费者 (SPMC) 顺序锁快照容器
 *
 * 特性：
 * 1. Writer 永远不阻塞 (Wait-free)。
 * 2. Reader 只取最新数据，若读取期间发生写入则重试 (Lock-free)。
 * 3. 适用于 "Conflation" 场景：只关心最新状态，允许丢弃旧数据。
 *
 * 内存布局：
 * [ seq (8B) ] [ padding ]
 * [ data (T) ...         ]
 */
template <typename T>
  requires ShmData<T>
class alignas(config::CACHE_LINE_SIZE) SeqLock {
  static_assert(std::atomic<uint64_t>::is_always_lock_free,
                "SeqLock requires lock-free std::atomic<uint64_t>");

private:
  // 版本号：偶数=空闲，奇数=正在写入
  // alignas 确保 seq_ 独占缓存行，避免 False Sharing
  alignas(config::CACHE_LINE_SIZE) std::atomic<uint64_t> seq_{0};

  // 数据区对齐，确保 data_ 不会和 seq_ 在同一个缓存行
  alignas(alignof(T) > config::CACHE_LINE_SIZE ? alignof(T)
                                               : config::CACHE_LINE_SIZE) T data_;

public:
  SeqLock() noexcept = default;

  // ===========================================================================
  // Writer 操作 (Wait-free)
  // ===========================================================================

  // PERF: 零拷贝写入 (直接在共享内存上构造/修改)
  // F: void(T& slot)
  template <typename F>
  void write(F &&writer) noexcept {
    // 1. seq -> 奇数 (开始写)
    uint64_t s = seq_.load(std::memory_order_relaxed);
    seq_.store(s + 1, std::memory_order_relaxed);

    // Release Fence: 确保 seq 变更对其他线程可见，且发生在数据修改之前
    std::atomic_thread_fence(std::memory_order_release);

    // 2. 写入数据
    std::forward<F>(writer)(data_);

    // Release Fence: 确保数据修改完成，且发生在 seq 变回偶数之前
    std::atomic_thread_fence(std::memory_order_release);

    // 3. seq -> 偶数 (写完)
    seq_.store(s + 2, std::memory_order_relaxed);
  }

  // PERF: 值拷贝写入
  void store(const T &val) noexcept {
    write([&val](T &slot) { slot = val; });
  }

  // ===========================================================================
  // Reader 操作 (Lock-free / Spin)
  // ===========================================================================

  // PERF: 尝试零拷贝读取 (Visitor 模式)
  // 如果读取期间数据发生变化，返回 false
  // F: void(const T& data)
  template <typename F>
  bool try_read(F &&visitor) const noexcept {
    // 1. 读取前版本号 (Acquire 语义)
    uint64_t s1 = seq_.load(std::memory_order_acquire);

    // 如果是奇数，说明正在写，数据是脏的
    if (s1 & 1) {
      return false;
    }

    // 2. 读取数据 (用户逻辑)
    // 注意：这里可能会读到撕裂的数据，但 ShmData<T> 保证 T 是 TriviallyCopyable，
    // 所以即使撕裂也不会 crash，只是数据无意义。
    std::forward<F>(visitor)(data_);

    // Acquire Fence: 确保数据读取发生在检查 s2 之前
    std::atomic_thread_fence(std::memory_order_acquire);

    // 3. 读取后版本号
    uint64_t s2 = seq_.load(std::memory_order_relaxed);

    // 4. 验证一致性
    return s1 == s2;
  }

  // PERF: 尝试值拷贝读取
  bool try_load(T &out) const noexcept {
    return try_read([&out](const T &slot) { out = slot; });
  }

  // PERF: 阻塞式零拷贝读取
  template <typename F>
  void read(F &&visitor) const noexcept {
    while (!try_read(visitor)) {
      cpu_relax();
    }
  }

  // PERF: 阻塞式值拷贝读取
  T load() const noexcept {
    T out;
    read([&out](const T &slot) { out = slot; });
    return out;
  }

  // ===========================================================================
  // 状态查询
  // ===========================================================================
  
  // 粗略检查是否正在被写入
  bool may_busy() const noexcept {
      return seq_.load(std::memory_order_relaxed) & 1;
  }
};

} // namespace eph
