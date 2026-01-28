#pragma once

// 平台特定的头文件包含
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
#include <immintrin.h>
#endif

namespace eph {

/**
 * @brief CPU 自旋等待策略 (Hint 指令)
 *
 * @details
 * 在自旋锁 (Spinlock) 或 Busy Wait 循环中调用此函数至关重要：
 * 1. **流水线优化**: `pause` (x86) 指令告诉 CPU 这是一个循环等待，防止 CPU 误判分支预测而清空流水线，减少退出循环时的性能惩罚。
 * 2. **功耗控制**: 降低 CPU 在自旋时的执行频率，减少发热和电能消耗。
 * 3. **超线程友好**: 在 Hyper-Threading 架构上，让出执行单元给同一个核心上的另一个硬件线程。
 */
inline void cpu_relax() noexcept {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) ||             \
    defined(_M_IX86)
  _mm_pause(); // 提示 CPU 这是一个自旋循环，降低功耗并避免流水线清空
#elif defined(__aarch64__)
  asm volatile("yield"); // ARM64
#else
  std::this_thread::yield(); // Fallback
#endif
}

} // namespace shm
