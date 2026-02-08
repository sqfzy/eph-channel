#pragma once

#include "eph/platform.hpp" // for cpu_relax
#include "eph/types.hpp"    // for ShmData, Align
#include <atomic>
#include <array>
#include <bit>
#include <optional>

namespace eph::benchmark {

/**
 * @brief [Baseline] 基于自旋锁的可丢弃 RingBuffer
 */
template <typename T, size_t Capacity>
requires ShmData<T>
class RingBuffer {
    static_assert(std::has_single_bit(Capacity), "Capacity must be power of 2");
    static constexpr size_t mask_ = Capacity - 1;

public:
    RingBuffer() noexcept = default;

    // =========================================================
    // 写入 (Producer)
    // =========================================================
    void push(const T& val) noexcept {
        lock();

        // 检查满：逻辑长度 >= 容量
        if (state_.head_ - state_.tail_ >= Capacity) {
            // 强制推进 tail，丢弃一个旧数据 (Lossy)
            state_.tail_++;
        }

        buffer_[state_.head_ & mask_] = val;
        state_.head_++;

        unlock();
    }

    // =========================================================
    // 读取 (Consumer)
    // =========================================================
    
    /**
     * @brief 阻塞式读取
     * 如果队列为空，会释放锁并自旋等待，直到有数据为止。
     */
    T pop() noexcept {
        while (true) {
            lock();

            // 检查是否为空
            if (state_.head_ > state_.tail_) {
                // 有数据：读取 -> 推进 tail -> 解锁 -> 返回
                T val = buffer_[state_.tail_ & mask_];
                state_.tail_++;
                unlock();
                return val;
            }

            // 无数据：解锁 -> 放松 CPU -> 重试
            // 必须在等待前解锁，否则 Producer 无法写入（死锁）
            unlock();
            eph::cpu_relax();
        }
    }

    /**
     * @brief 非阻塞尝试读取 (可选接口)
     */
    std::optional<T> try_pop() noexcept {
        std::optional<T> result = std::nullopt;
        lock();
        if (state_.head_ > state_.tail_) {
            result = buffer_[state_.tail_ & mask_];
            state_.tail_++;
        }
        unlock();
        return result;
    }

    // =========================================================
    // 状态查询
    // =========================================================
    
    size_t size() const noexcept {
        return state_.head_ - state_.tail_;
    }

    bool empty() const noexcept {
        return size() == 0;
    }

private:
    // =========================================================
    // 数据成员
    // =========================================================
    
    alignas(Align<T>) std::array<T, Capacity> buffer_;

    // FIX: 命名结构体 "State" 以修复 C++ 标准对匿名聚合体的限制
    struct alignas(detail::CACHE_LINE_SIZE) State {
        std::atomic<bool> locked_{false};
        uint64_t head_{0};
        uint64_t tail_{0};
    } state_;

    // =========================================================
    // TTAS 自旋锁实现
    // =========================================================
    void lock() noexcept {
        while (true) {
            // 1. Test (Relaxed Read)
            // 如果锁已经被持有，就在这里自旋读取缓存，不触发总线锁定
            if (!state_.locked_.load(std::memory_order_relaxed)) {
                // 2. Test-and-Set (Acquire Write)
                // 尝试获取锁
                if (!state_.locked_.exchange(true, std::memory_order_acquire)) {
                    return; // 获取成功
                }
            }
            // 3. Pause
            eph::cpu_relax();
        }
    }

    void unlock() noexcept {
        state_.locked_.store(false, std::memory_order_release);
    }
};

} // namespace eph::benchmark
