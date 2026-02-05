#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <chrono>
#include <concepts>
#include <cstdint>
#include <format>
#include <functional>
#include <print>

// 平台检测与内联宏定义
#if defined(_MSC_VER)
#include <intrin.h>
#define ALWAYS_INLINE __forceinline
#elif defined(__x86_64__) || defined(__i386__)
#include <x86intrin.h>
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#elif defined(__aarch64__)
#define ALWAYS_INLINE __attribute__((always_inline)) inline
#else
#define ALWAYS_INLINE inline
#endif

namespace eph::benchmark {

/**
 * @brief 告诉编译器该变量被"使用"了，防止被优化掉。
 * * 原理：通过内联汇编创建一个假的依赖关系。
 * - "g": 让编译器选择寄存器或内存(General)
 * - "memory": 内存屏障，防止读写操作被重排到此指令之外
 */
template <typename T>
ALWAYS_INLINE void do_not_optimize(T&& value) noexcept {
#if defined(__clang__) || defined(__GNUC__)
    // "r,m" 表示输入可以是寄存器(r)或内存(m)。
    // 对于大结构体，编译器会自动选择 m，避免不必要的寄存器拷贝。
    asm volatile("" : : "r,m"(value) : "memory");
#else
    // MSVC fallback: 强制读取 volatile 内存
    // 注意：这在 MSVC 上可能会产生极小的指令开销，但在 Linux/GCC/Clang 下不会走到这里
    static volatile char sink; 
    const char* p = reinterpret_cast<const char*>(&value);
    sink = *p; 
    _ReadWriteBarrier(); // 编译器屏障
#endif
}

/**
 * @brief 显式的编译器屏障 (Clobber Memory)。
 * * 告诉编译器：此点之后内存状态未知，所有缓存于寄存器的变量必须写回内存，
 * 且禁止将此点前后的内存访问指令重排越过此屏障。
 */
ALWAYS_INLINE void clobber_memory() noexcept {
#if defined(__clang__) || defined(__GNUC__)
    asm volatile("" : : : "memory");
#elif defined(_MSC_VER)
    _ReadWriteBarrier();
#endif
}


// =========================================================
// 1. TSC (Time Stamp Counter) - 高精度计时核心
// =========================================================
class TSC {
public:
    // 获取当前 CPU 周期数
    [[nodiscard]] static ALWAYS_INLINE uint64_t now() noexcept {
#if defined(__x86_64__) || defined(_M_X64)
        unsigned int aux;
        // lfence 确保前面的指令全部完成，rdtscp 序列化读，防止乱序
        uint64_t tsc = __rdtscp(&aux);
        std::atomic_signal_fence(std::memory_order_seq_cst);
        return tsc;
#elif defined(__aarch64__) || defined(_M_ARM64)
        uint64_t val;
        // isb 充当指令屏障，确保流水线排空
        asm volatile("isb; mrs %0, cntvct_el0" : "=r"(val) : : "memory");
        return val;
#else
#error "Current architecture does not support hardware TSC."
#endif
    }

    // 初始化并校准频率
    static void init(std::chrono::milliseconds duration = std::chrono::milliseconds(200)) {
        std::print("[Timer] Calibrating... ");

        // 1. 预热：让 CPU 退出节能状态并填充指令缓存
        auto warm_up = [](auto dur) {
            auto s = std::chrono::steady_clock::now();
            while (std::chrono::steady_clock::now() - s < dur) {
                std::atomic_signal_fence(std::memory_order_relaxed);
            }
        };
        warm_up(std::chrono::milliseconds(20));

        // 2. 采样
        const auto t1 = std::chrono::steady_clock::now();
        const uint64_t c1 = now();

        warm_up(duration);

        const uint64_t c2 = now();
        const auto t2 = std::chrono::steady_clock::now();

        // 3. 计算倍率
        double ns_total = std::chrono::duration<double, std::nano>(t2 - t1).count();
        double cycles_total = static_cast<double>(c2 - c1);

        ns_per_cycle_ = ns_total / std::max(1.0, cycles_total);
        std::println("CPU Frequency: {:.2f} GHz", 1.0 / ns_per_cycle_);
    }

    [[nodiscard]] static double to_ns(uint64_t cycles) noexcept {
        return static_cast<double>(cycles) * ns_per_cycle_;
    }

    template <typename Rep = double>
    [[nodiscard]] static uint64_t to_cycles(Rep ns) noexcept {
        return static_cast<uint64_t>(static_cast<double>(ns) / ns_per_cycle_);
    }

    template <class Rep, class Period>
    [[nodiscard]] static uint64_t to_cycles(std::chrono::duration<Rep, Period> d) noexcept {
        return to_cycles(std::chrono::duration<double, std::nano>(d).count());
    }

private:
    static inline double ns_per_cycle_ = 0.0;
};

// =========================================================
// 2. 测量原语
// =========================================================

// RAII 计时器 (Scope Guard)
class ScopedTSC {
public:
  explicit ALWAYS_INLINE ScopedTSC(uint64_t &out_cycles)
      : out_cycles_(out_cycles), start_cycles_(TSC::now()) {}

  ALWAYS_INLINE ~ScopedTSC() { out_cycles_ = TSC::now() - start_cycles_; }

  ScopedTSC(const ScopedTSC &) = delete;
  ScopedTSC &operator=(const ScopedTSC &) = delete;

private:
  uint64_t &out_cycles_;
  uint64_t start_cycles_;
};

// 函数测量包装器 (Function Wrapper)
template <typename Func, typename... Args>
  requires std::invocable<Func, Args...>
[[nodiscard]] ALWAYS_INLINE uint64_t measure(Func &&func, Args &&...args) {
  const uint64_t start = TSC::now();
  std::invoke(std::forward<Func>(func), std::forward<Args>(args)...);
  const uint64_t end = TSC::now();
  return end - start;
}

} // namespace eph::benchmark
