#pragma once

#include "eph/platform.hpp"
#include "eph/types.hpp"
#include <atomic>
#include <cassert>
#include <cstring>

namespace eph {

using namespace eph::detail;

/**
 * @brief 多缓冲顺序锁 (Buffered SeqLock)
 *
 * 核心优势：
 * 1. 消除 Cache Thrashing：读写操作发生在不同的 Cache Line 上。
 * 2. 极高的并发吞吐：Writer 永不阻塞，Reader 极少重试。
 *
 * @tparam T 数据类型 (需满足 TriviallyCopyable)
 * @tparam N 缓冲槽位数量，必须是 2 的幂 (推荐 4, 8, 16)。
 * 越大容错率越高，但占用内存越多。
 */

template <typename T, size_t N = 8>
  requires ShmData<T>
class alignas(BufferAlign<T>) SeqLockBuffer {
  static_assert((N & (N - 1)) == 0, "Buffer size N must be power of 2");
  static_assert(N >= 2, "Buffer size N must be at least 2");

private:
  struct alignas(BufferAlign<T>) Slot {
    // 槽位版本号：
    // 偶数 = 数据稳定
    // 奇数 = 正在写入
    // 使用 Relaxed 原子操作即可，依靠 Release-Acquire 链保证顺序
    std::atomic<uint64_t> seq{0};

    T data_{};
  };

  // 环形缓冲槽位数组
  Slot slots_[N];

  // 全局单调递增索引，指向“最新已完成写入”的槽位下标
  alignas(BufferAlign<T>) std::atomic<uint64_t> global_index_{0};

  // 尾部 Padding，防止与其他临近变量发生伪共享
  char pad2_[BufferAlign<T> - sizeof(std::atomic<uint64_t>)];

public:
  SeqLockBuffer() noexcept {
    // 初始化确保所有 slot 的 seq 都是偶数
    for (auto &s : slots_) {
      s.seq.store(0, std::memory_order_relaxed);
    }
    // 初始指向 0 号槽位
    global_index_.store(0, std::memory_order_relaxed);
  }

  // ===========================================================================
  // Writer 操作 (Wait-free)
  // ===========================================================================

  /**
   * @brief 写入新数据
   * @param val 要写入的数据对象
   */
  void store(const T &val) noexcept {
    // 1. 获取当前索引，计算下一个写入位置
    // Relaxed 即可，因为我们只关心计算下一个位置，不依赖之前的同步关系
    const uint64_t current_idx = global_index_.load(std::memory_order_relaxed);
    const uint64_t next_idx = current_idx + 1;

    Slot &s = slots_[next_idx & (N - 1)];

    // 2. [关键] 获取该槽位之前的 seq，并标记为正在写入 (变为奇数)
    // 这里的 seq 是为了防止 Writer 绕了一圈追尾覆盖 Reader 正在读的数据
    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);

    // 3. 执行数据拷贝 (无锁写入)
    s.data_ = val;

    // 4. 结束写入：seq + 1 变回偶数
    // Release 语义保证 Reader 看到偶数 seq 时，数据一定已经写完了
    s.seq.store(start_seq + 2, std::memory_order_release);

    // 5. 发布全局索引：原子更新 Head 指针
    // Release 语义保证 Reader 拿到新 index 时，对应的 slot 写入已完成
    global_index_.store(next_idx, std::memory_order_release);
  }

  /**
   * @brief 零拷贝写入 (就地构造)
   * @tparam F Lambda 类型: void(T& slot_data)
   */
  template <typename F> void write(F &&writer) noexcept {
    const uint64_t current_idx = global_index_.load(std::memory_order_relaxed);
    const uint64_t next_idx = current_idx + 1;

    Slot &s = slots_[next_idx & (N - 1)];

    uint64_t start_seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(start_seq + 1, std::memory_order_release);

    // 原地构造/修改
    std::forward<F>(writer)(s.data_);

    s.seq.store(start_seq + 2, std::memory_order_release);
    global_index_.store(next_idx, std::memory_order_release);
  }

  // ===========================================================================
  // Reader 操作 (Lock-free)
  // ===========================================================================

  /**
   * @brief 尝试读取最新数据
   * @param out [输出] 读取到的数据
   * @return true 成功读取；false 数据不一致（读取期间被覆盖），需要重试
   */
  bool try_load(T &out) const noexcept {
    // 1. 获取当前最新的全局索引
    // Acquire 语义与 Writer 的 store(global_index_) 配对
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    const Slot &s = slots_[idx & (N - 1)];

    // 2. 读取槽位版本号
    // Acquire 语义确保看到 Writer 对 data 的修改
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);

    // 检查 1: 如果 seq 是奇数，说明 Writer 刚好绕了一圈正在写这个槽位
    // (极低概率)
    if (seq1 & 1)
      return false;

    // 3. 拷贝数据
    // 此时 Writer 正在写 slots_[(idx+1)%N]，物理内存完全隔离，Cache Hit 率极高
    out = s.data_;

    // 内存屏障：确保数据拷贝完成后再检查 seq
    std::atomic_thread_fence(std::memory_order_acquire);

    // 4. 再次读取版本号
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    // 检查 2: 如果 seq 变了，说明拷贝期间 Writer 疯狂写入并覆盖了此槽位
    return seq1 == seq2;
  }

  /**
   * @brief 零拷贝读取 (Visitor 模式)
   * @tparam F Lambda 类型: void(const T& data)
   */
  template <typename F> bool try_read(F &&visitor) const noexcept {
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    const Slot &s = slots_[idx & (N - 1)];

    uint64_t seq1 = s.seq.load(std::memory_order_acquire);
    if (seq1 & 1)
      return false;

    std::forward<F>(visitor)(s.data_);

    std::atomic_thread_fence(std::memory_order_acquire);
    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);

    return seq1 == seq2;
  }

  /**
   * @brief 阻塞式读取 (自旋直到成功)
   * 慎用：在极其极端的竞争下可能导致短暂的 CPU 自旋，但在 Buffered
   * 模式下几乎不可能发生。
   */
  T load() const noexcept {
    T out;
    while (!try_load(out)) {
      eph::cpu_relax();
    }
    return out;
  }
};

} // namespace eph
