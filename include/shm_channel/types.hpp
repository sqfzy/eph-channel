#pragma once

#include <atomic>
#include <cstddef>
#include <new>

// 平台特定的 pause 指令
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <immintrin.h>
#endif

namespace shm {

namespace config {
constexpr size_t CACHE_LINE_SIZE = 64;
constexpr size_t DEFAULT_CAPACITY = 1024;
} // namespace config

// 约束 T 必须能在共享内存中安全使用
// 1. 必须是平凡可拷贝的 (没有虚函数，没有自定义拷贝构造逻辑，类似于 memcpy)
// 2. 必须是默认可构造的 (RingBuffer 初始化需要)
template <typename T>
concept Shmable =
    std::is_trivially_copyable_v<T> && std::is_default_constructible_v<T>;

// 检查原子操作在共享内存中是否免锁 (Linux x86_64 通常是 true)
// 如果不是免锁的，原子变量可能使用进程本地的哈希表锁，导致无法跨进程同步
template <typename T>
concept LockFreeAtomic = std::atomic<T>::is_always_lock_free;

// CPU 自旋等待策略
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
  _mm_pause();
#elif defined(__aarch64__)
  asm volatile("yield"); // ARM64
#else
  std::this_thread::yield(); // Fallback
#endif
}

} // namespace shm
