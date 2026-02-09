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
 * @brief 多缓冲顺序锁环形队列 (Buffered SeqLock RingBuffer)
 *
 * 这是一个针对单生产者、多消费者 (SPMC) 场景优化的无锁数据结构。
 * 它结合了 RingBuffer 的缓冲能力与 SeqLock 的版本控制机制。
 *
 * @section 特性
 * 1. **Writer Wait-free**: 写入者仅需更新本地索引和原子序列号，无需自旋或阻塞。
 * 2. **Reader Lock-free**: 读取者通过乐观读取 (Optimistic Read)
 * 获取数据，极低概率重试。
 * 3. **Shadow Indexing**:
 * 写入者维护本地影子索引，避免对共享的全局索引进行原子读取
 * (RMW)，降低总线竞争。
 * 4. **Cache Friendly**: 核心数据结构经过严格的 Cache Line 对齐，消除伪共享
 * (False Sharing)。
 *
 * @tparam T 数据类型 (必须满足 TriviallyCopyable 概念，以允许位拷贝)
 * @tparam N 缓冲槽位数量，必须是 2 的幂。
 */
template <typename T, size_t N = 8>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer {
  static_assert((N & (N - 1)) == 0, "Buffer size N must be power of 2");
  static_assert(
      N > 1, "Primary template requires N > 1, use N=1 for single slot mode");

private:
  /**
   * @brief 内部存储槽位
   *
   * 包含版本控制序列号和实际数据。
   * 版本号机制：
   * - 偶数 (Even): 数据处于稳定一致状态，可安全读取。
   * - 奇数 (Odd): 数据正在被写入，处于不一致状态。
   */
  struct Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };

  // ---------------------------------------------------------------------------
  // 核心存储区
  // ---------------------------------------------------------------------------

  /// 环形缓冲槽位数组
  Slot slots_[N];

  // ---------------------------------------------------------------------------
  // Writer 独占区 (Hot Data)
  // ---------------------------------------------------------------------------

  /**
   * @brief 影子索引 (Shadow Index)
   *
   * 仅由 Writer 维护和访问的本地索引副本。
   * 作用：Writer 在计算写入位置时，无需读取可能被 Reader 频繁访问的
   * global_index_， 从而避免引发 Cache Coherency Traffic (缓存一致性流量)。
   */
  alignas(Align<T>) uint64_t writer_index_{0};

  // ---------------------------------------------------------------------------
  // Reader 共享区 (Hot Data)
  // ---------------------------------------------------------------------------

  /**
   * @brief 全局索引 (Head Pointer)
   *
   * 指向“最新已完成写入”的槽位下标（单调递增）。
   * - Writer: 只写 (Release语义)。
   * - Reader: 只读 (Acquire语义)。
   *
   * @note 通过 alignas 强制将其置于独立的 Cache Line，防止与 writer_index_
   * 发生伪共享。
   */
  alignas(Align<T>) std::atomic<uint64_t> global_index_{0};

  /// 尾部填充，防止与内存中紧邻的后续对象发生伪共享。
  char pad_tail_[Align<T> - sizeof(std::atomic<uint64_t>)];

public:
  RingBuffer() noexcept {
    for (auto &s : slots_) {
      s.seq.store(0, std::memory_order_relaxed);
    }
    // 初始化指向 0 号槽位
    global_index_.store(0, std::memory_order_relaxed);
    writer_index_ = 0;
  }

  // ===========================================================================
  // Writer 操作 (Wait-free, Single Producer)
  // ===========================================================================

  /**
   * @brief 写入新数据
   *
   * 这是一个无等待操作。如果写入速度远超读取速度，旧数据将被覆盖。
   *
   * @param val 要写入的数据对象
   */
  void push(const T &val) noexcept {
    // 1. 获取下一个写入位置 (使用本地 Shadow Index，无原子开销)
    const uint64_t current_idx = writer_index_;
    const uint64_t next_idx = current_idx + 1;

    Slot &s = slots_[next_idx & (N - 1)];

    // 2. 标记开始写入：seq 变为奇数
    // 使用 Relaxed load 因为此处只有 Writer 线程修改 seq
    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);

    // 使用 Release 语义确保后续的数据写入不会被重排到此操作之前
    s.seq.store(start_seq + 1, std::memory_order_release);

    // 3. 执行数据拷贝 (Payload Write)
    s.data_ = val;

    // 4. 标记结束写入：seq + 1 变回偶数
    // Release 语义确保数据写入对 Reader 可见后，seq 才会更新
    s.seq.store(start_seq + 2, std::memory_order_release);

    // 5. 发布全局索引
    // Release 语义确保上述 Slot 的所有变更对获取了 global_index_ 的 Reader 可见
    global_index_.store(next_idx, std::memory_order_release);

    // 6. 更新本地影子索引
    writer_index_ = next_idx;
  }

  /**
   * @brief 零拷贝写入 (In-place Construction)
   *
   * 允许直接在 RingBuffer
   * 的内部存储中构造或修改对象，避免中间临时对象的拷贝开销。
   *
   * @tparam F 回调函数类型: void(T& slot_data)
   * @param writer 用于填充数据的回调函数
   */
  template <typename F> void write(F &&writer) noexcept {
    const uint64_t current_idx = writer_index_;
    const uint64_t next_idx = current_idx + 1;

    Slot &s = slots_[next_idx & (N - 1)];

    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);

    // 原地构造/修改数据
    std::forward<F>(writer)(s.data_);

    s.seq.store(start_seq + 2, std::memory_order_release);
    global_index_.store(next_idx, std::memory_order_release);

    writer_index_ = next_idx;
  }

  // ===========================================================================
  // Reader 操作 (Lock-free)
  // ===========================================================================

  /**
   * @brief 尝试进行零拷贝读取 (Visitor Pattern)
   *
   * 使用乐观读取 (Optimistic Read) 策略。
   * 如果在读取过程中数据被 Writer 修改（版本号不一致），则操作失败。
   *
   * @tparam F 回调函数类型: void(const T& data)
   * @param visitor 用于访问数据的回调函数
   * @return true 读取成功且数据一致; false 读取期间发生写入，数据可能已损坏
   */
  template <typename F> bool try_read(F &&visitor) const noexcept {
    // 1. 获取当前最新的全局索引 (Acquire 语义，与 Writer 的 Release 匹配)
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    const Slot &s = slots_[idx & (N - 1)];

    // 2. 读取开始前的版本号 (Acquire 语义)
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);

    // 如果版本号为奇数，说明 Writer 正在写入，立即失败
    if (seq1 & 1)
      return false;

    // 3. 执行用户回调访问数据
    std::forward<F>(visitor)(s.data_);

    // 4. 内存屏障 (Load-Load Fence)
    // 强制 CPU 保证先完成上述数据的读取，再读取下方的 seq2
    std::atomic_thread_fence(std::memory_order_acquire);

    // 5. 再次读取版本号检查一致性
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  /**
   * @brief 尝试读取最新数据 (值拷贝)
   * @param out [out] 读取成功时填充目标对象
   * @return true 成功; false 发生竞争
   */
  bool try_pop_latest(T &out) const noexcept {
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    const Slot &s = slots_[idx & (N - 1)];

    uint64_t seq1 = s.seq.load(std::memory_order_acquire);

    if (seq1 & 1)
      return false;

    // 数据拷贝
    out = s.data_;

    // 确保数据拷贝完成后再检查 seq
    std::atomic_thread_fence(std::memory_order_acquire);

    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  /**
   * @brief 阻塞式读取 (自旋直到成功)
   */
  void pop_latest(T &out) const noexcept {
    while (!try_pop_latest(out)) {
      cpu_relax();
    }
  }

  /**
   * @brief 阻塞式读取 (自旋直到成功)
   */
  T pop_latest() const noexcept {
    T out;
    while (!try_pop_latest(out)) {
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

/**
 * @brief 三重缓冲特化版 (Triple Buffer / N=3)
 *
 * 针对 N=3 场景（如 Front-Back-Middle 模式）进行的特定指令级优化实现。
 * 相比于通用的 N=Pow2 实现，此特化版本直接处理物理索引 (0, 1, 2) 的循环，
 * 避免了通用除法或取模运算，并利用了小范围整数的寄存器优化。
 *
 * @section 内存布局
 * [ Slot 0 ] [ Slot 1 ] [ Slot 2 ]
 * [ writer_idx (Shadow) ] -- Cache Line Padding -- [ global_idx (Shared) ]
 */
template <typename T>
  requires ShmData<T>
class alignas(Align<T>) RingBuffer<T, 3> {

private:
  struct alignas(Align<T>) Slot {
    // 槽位版本号 (单调递增)
    std::atomic<uint64_t> seq{0};
    T data_{};
  };

  // ---------------------------------------------------------------------------
  // 核心存储区
  // ---------------------------------------------------------------------------
  Slot slots_[3];

  // ---------------------------------------------------------------------------
  // Writer 独占区 (Hot Data)
  // ---------------------------------------------------------------------------

  /**
   * @brief 影子索引 (Shadow Index)
   *
   * 存储当前最新的物理下标 (0, 1, or 2)。
   * 仅 Writer 可见，无需原子操作。
   */
  alignas(Align<T>) uint8_t writer_idx_{0};

  // ---------------------------------------------------------------------------
  // Reader 共享区 (Hot Data)
  // ---------------------------------------------------------------------------

  /**
   * @brief 全局索引 (Head Pointer)
   *
   * 指向当前最新的、可读的槽位物理下标 (0, 1, or 2)。
   *
   * @note 这里的索引是循环复用的非单调值，而非 N=PowerOf2 版本中的单调递增值。
   * 虽然索引值非单调，但数据的一致性仍由 Slot 内部的单调 seq 保证。
   * 此处的 global_idx_ 仅作为给 Reader 的 "Hint"。
   */
  alignas(Align<T>) std::atomic<uint8_t> global_idx_{0};

  /// 尾部填充，防止与内存中紧邻的后续对象发生伪共享。
  char pad_tail_[Align<T> - sizeof(std::atomic<uint8_t>)];

public:
  RingBuffer() noexcept {
    for (auto &s : slots_) {
      s.seq.store(0, std::memory_order_relaxed);
    }
    // 初始状态：指向 Slot 0
    global_idx_.store(0, std::memory_order_relaxed);
    writer_idx_ = 0;
  }

  // ===========================================================================
  // Writer 操作 (Wait-free)
  // ===========================================================================

  /**
   * @brief 计算下一个物理槽位下标 (0->1->2->0)
   *
   * 编译器优化提示：对于小整数的三态循环，编译器通常会优化为条件传送 (cmov)
   * 或简单的比较跳转，这比通用的 DIV/MOD 指令延迟更低。
   */
  static constexpr uint8_t next_slot(uint8_t current) noexcept {
    return (current + 1 == 3) ? 0 : current + 1;
  }

  template <typename F> void write(F &&writer) noexcept {
    // 1. 使用影子索引计算下一个写入位置
    // Writer 总是写入 (Current + 1)，确保不会覆写 Reader 可能正在读取的 Current
    const uint8_t next_idx = next_slot(writer_idx_);

    Slot &s = slots_[next_idx];

    // 2. 锁定槽位 (Seq 变奇数)
    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);

    // 3. 写入数据
    std::forward<F>(writer)(s.data_);

    // 4. 解锁槽位 (Seq 变偶数)
    s.seq.store(start_seq + 2, std::memory_order_release);

    // 5. 发布全局位置 (Release 语义)
    global_idx_.store(next_idx, std::memory_order_release);

    // 6. 更新本地影子索引
    writer_idx_ = next_idx;
  }

  void push(const T &val) noexcept {
    write([&val](T &slot) { slot = val; });
  }

  // ===========================================================================
  // Reader 操作 (Lock-free)
  // ===========================================================================

  template <typename F> bool try_read(F &&visitor) const noexcept {
    // 1. 获取当前最新位置 (0, 1, or 2)
    // 这是一个非单调的物理索引，直接作为数组下标使用
    const uint8_t idx = global_idx_.load(std::memory_order_acquire);

    // 安全检查：防止越界 (防御性编程，虽然逻辑上不应发生)
    if (idx >= 3) [[unlikely]]
      return false;

    const Slot &s = slots_[idx];

    // 2. 乐观锁读取协议
    // 读取开始 seq (Acquire)
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false;

    std::forward<F>(visitor)(s.data_);

    // Load-Load Barrier
    std::atomic_thread_fence(std::memory_order_acquire);

    // 读取结束 seq (Relaxed)
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  bool try_pop_latest(T &out) const noexcept {
    // 1. 获取索引
    const uint8_t idx = global_idx_.load(std::memory_order_acquire);
    if (idx >= 3) [[unlikely]]
      return false;

    const Slot &s = slots_[idx];

    // 2. 检查版本
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false;

    // 3. 拷贝数据
    out = s.data_;
    std::atomic_thread_fence(std::memory_order_acquire);

    // 4. 验证版本
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);
    return seq1 == seq2;
  }

  T pop_latest() const noexcept {
    T out;
    while (!try_pop_latest(out)) {
      cpu_relax();
    }
    return out;
  }
};

/**
 * @brief 顺序锁 (SeqLock) - RingBuffer 的单槽位特化版本 (N=1)
 *
 * 这是一个标准的单生产者-多消费者 (SPMC) 顺序锁实现。
 * 相比通用的 RingBuffer，它移除了 global_index
 * 的维护开销，具有极致的内存紧凑性。
 *
 * @note 内存布局与伪共享：
 * 虽然代码尝试将类对齐到 Cache Line，但由于 N=1，Seq(8B) 和 Data(T)
 * 通常位于同一个 Cache Line 中。这意味着读写操作会竞争同一个 Cache Line，
 * 在高并发场景下可能导致 Ping-pong 效应。适用于写入频率较低或 T
 * 体积极小的场景。
 *
 * @tparam T 数据类型
 */
template <typename T>
  requires ShmData<T>
class alignas(alignof(T) > CACHE_LINE_SIZE ? alignof(T) : CACHE_LINE_SIZE)
    RingBuffer<T, 1> {

  static_assert(std::atomic<uint64_t>::is_always_lock_free,
                "SeqLock requires lock-free std::atomic<uint64_t>");

private:
  // 版本号：偶数=空闲，奇数=正在写入
  // alignas 确保 seq_ 位于 Cache Line 起始位置
  alignas(CACHE_LINE_SIZE) std::atomic<uint64_t> seq_{0};

  // 数据域：如果 alignof(T) 较小，它将紧随 seq_ 之后
  alignas(alignof(T) > CACHE_LINE_SIZE ? alignof(T)
                                       : CACHE_LINE_SIZE) T data_{};

public:
  RingBuffer() noexcept = default;

  // ===========================================================================
  // Writer 操作 (Wait-free)
  // ===========================================================================

  /**
   * @brief 零拷贝写入
   *
   * @tparam F 回调函数类型: void(T& slot_data)
   */
  template <typename F> void write(F &&writer) noexcept {
    uint64_t seq = seq_.load(std::memory_order_relaxed);

    // 1. 开始写入：序列号+1（变奇数）
    seq_.store(seq + 1, std::memory_order_relaxed);

    // 2. Store-Store Barrier
    // 确保“序列号变奇数”这一操作对其他核心可见后，才开始写入数据
    std::atomic_thread_fence(std::memory_order_release);

    // 3. 写入数据
    std::forward<F>(writer)(data_);

    // 4. Store-Store Barrier
    // 确保数据完全写入后，才更新序列号
    std::atomic_thread_fence(std::memory_order_release);

    // 5. 完成写入：序列号+1（变偶数）
    seq_.store(seq + 2, std::memory_order_relaxed);
  }

  /// 值拷贝写入
  void push(const T &val) noexcept {
    write([&val](T &slot) { slot = val; });
  }

  // ===========================================================================
  // Reader 操作 (Lock-free / Spin)
  // ===========================================================================

  /**
   * @brief 尝试零拷贝读取 (Visitor Pattern)
   *
   * @warning 关于数据竞争 (Data Race) 的说明：
   * 在 C++ 内存模型中，如果此时 Writer 正在写入 data_，而 Reader 正在读取
   * data_， 严格来说构成了 Data Race，属于未定义行为 (UB)。 但是，通过
   * ShmData<T> 约束 T 必须是 TriviallyCopyable，在现代主流架构 (x86/ARM) 上，
   * 这种读取是安全的——即便读到了撕裂的(torn)数据，程序也不会崩溃。
   * 我们通过前后的 seq 校验来丢弃撕裂的数据。
   *
   * @return true 读取成功; false 数据脏
   */
  template <typename F> bool try_read(F &&visitor) const noexcept {
    // 1. 读取开始版本号
    uint64_t seq0 = seq_.load(std::memory_order_relaxed);

    // 如果是奇数，说明正在写，数据是脏的
    if (seq0 & 1) {
      return false;
    }

    // 2. Load-Load Barrier
    // 确保先读取版本号，再读取后续数据
    std::atomic_thread_fence(std::memory_order_acquire);

    // 3. 乐观读取数据
    std::forward<F>(visitor)(data_);

    // 4. Load-Load Barrier
    // 确保数据读取完成后，再读取结束版本号
    std::atomic_thread_fence(std::memory_order_acquire);

    // 5. 再次读取序列号
    uint64_t seq1 = seq_.load(std::memory_order_relaxed);

    // 6. 验证一致性
    return seq0 == seq1;
  }

  /// 尝试值拷贝读取
  bool try_pop_latest(T &out) const noexcept {
    return try_read([&out](const T &slot) { out = slot; });
  }

  /// 阻塞式零拷贝读取
  template <typename F> void read(F &&visitor) const noexcept {
    while (!try_read(visitor)) {
      cpu_relax();
    }
  }

  /// 阻塞式值拷贝读取
  T pop_latest() const noexcept {
    T out;
    read([&out](const T &slot) { out = slot; });
    return out;
  }

  // ===========================================================================
  // 状态查询
  // ===========================================================================

  /// 检查当前是否被写锁占用 (非原子快照，仅供参考)
  bool busy() const noexcept {
    return seq_.load(std::memory_order_relaxed) & 1;
  }
};

} // namespace eph
