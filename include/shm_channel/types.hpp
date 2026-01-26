#pragma once

#include <atomic>

namespace shm {

namespace config {
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t DEFAULT_CAPACITY = 1024;
} // namespace config

// [数据约束]
// 约束 RingBuffer 中存放的元素 T：
// 1. 必须是平凡可拷贝的 (memcpy 安全，无虚函数，无自定义拷贝逻辑)
// 2. 必须是默认可构造的
template <typename T>
concept ShmData =
    std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;

// [容器约束]
// 约束存放在 SharedMemory 中的容器对象 (如 RingBuffer)：
// 1. 必须是标准布局 (内存布局可预测，这是跨进程共享的基础)
// 2. 必须是默认可构造的 (SharedMemory 需要 inplace new)
template <typename T>
concept ShmLayout =
    std::is_standard_layout_v<T> && std::is_default_constructible_v<T>;

// 检查原子操作在共享内存中是否免锁 (Linux x86_64 通常是 true)
// 如果不是免锁的，原子变量可能使用进程本地的哈希表锁，导致无法跨进程同步
template <typename T>
concept LockFreeAtomic = std::atomic<T>::is_always_lock_free;

} // namespace shm
