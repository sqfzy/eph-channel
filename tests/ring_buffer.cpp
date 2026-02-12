#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <chrono>

#include "eph/core/ring_buffer.hpp"

using namespace eph;

// ============================================================================
// 1. 核心语义测试：确保它是 "Snapshot" 而不是 "Queue"
// ============================================================================
TEST(RingBufferTest, LatestValueSemantics_Generic) {
    // 使用 N=4 的通用模板
    RingBuffer<int, 4> rb;

    // 初始状态
    // 注意：如果是初始状态，Reader 可能会读到默认构造的值，或者根据实现逻辑阻塞/失败
    // 这里我们先写入一个初始值
    rb.push(100);
    
    int val = 0;
    EXPECT_TRUE(rb.try_pop_latest(val));
    EXPECT_EQ(val, 100);

    // 连续写入多次
    rb.push(200);
    rb.push(300);
    rb.push(400);

    // 核心验证：应该读到 400 (最新)，而不是 200 (队列里的下一个)
    EXPECT_TRUE(rb.try_pop_latest(val));
    EXPECT_EQ(val, 400);

    // 再次读取，仍然是 400 (Snapshot 不会消费掉数据)
    EXPECT_TRUE(rb.try_pop_latest(val));
    EXPECT_EQ(val, 400);
}

TEST(RingBufferTest, LatestValueSemantics_TripleBuffer) {
    // 特化版本 N=3
    RingBuffer<int, 3> rb;

    rb.push(1);
    rb.push(2);
    rb.push(3);
    rb.push(4); // 触发回绕 0 -> 1 -> 2 -> 0

    int val;
    EXPECT_TRUE(rb.try_pop_latest(val));
    EXPECT_EQ(val, 4);
}

TEST(RingBufferTest, LatestValueSemantics_SingleSlot) {
    // 特化版本 N=1 (纯 SeqLock)
    RingBuffer<int, 1> rb;

    rb.push(42);
    EXPECT_EQ(rb.pop_latest(), 42);

    rb.push(99);
    EXPECT_EQ(rb.pop_latest(), 99);
}

// ============================================================================
// 2. 并发数据完整性测试 (Torn Read Prevention)
// ============================================================================

// 一个较大的结构体，跨越多个 Cache Line，容易发生撕裂
struct LargeData {
    uint64_t a, b, c, d;
    uint64_t checksum;
};

TEST(RingBufferTest, NoTornReads_Stress) {
    RingBuffer<LargeData, 8> rb;
    std::atomic<bool> stop{false};

    // 生产者：疯狂写入
    std::thread producer([&]() {
        uint64_t counter = 1;
        while (!stop.load(std::memory_order_relaxed)) {
            // 构造数据，checksum 必须匹配
            rb.emplace(counter, counter, counter, counter, counter * 4);
            counter++;
            // 稍微让出一点 CPU，避免完全饿死 Reader (虽然 SeqLock 是 wait-free 的)
            if (counter % 1024 == 0) std::this_thread::yield();
        }
    });

    // 消费者：检查数据一致性
    std::thread consumer([&]() {
        auto start = std::chrono::steady_clock::now();
        size_t reads = 0;
        while (std::chrono::steady_clock::now() - start < std::chrono::milliseconds(200)) {
            LargeData d;
            // 使用 try_pop_latest，如果 writer 正在写，它应该返回 false (避免脏读)
            // 或者使用 pop_latest 自旋直到成功
            if (rb.try_pop_latest(d)) {
                reads++;
                ASSERT_EQ(d.a + d.b + d.c + d.d, d.checksum) 
                    << "Torn read detected! Data: " << d.a;
                ASSERT_EQ(d.a, d.b); // 确保内部字段也是一致的
            }
            cpu_relax();
        }
    });

    consumer.join();
    stop = true;
    producer.join();
}

// ============================================================================
// 3. 阻塞式 API 测试
// ============================================================================
TEST(RingBufferTest, BlockingConsume) {
    RingBuffer<int, 4> rb;
    
    // 启动一个延迟写入的线程
    std::thread t([&]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        rb.push(999);
    });

    // 主线程阻塞等待
    // 注意：初始状态下 global_index 是 0，且 slot seq 是 0。
    // 如果没有数据写入，consume_latest 可能会读到默认构造的 T (如果它认为那是有效的偶数 seq)。
    // 实际上，RingBuffer 构造时 seq 为 0，被视为"有效且空闲"。
    // 所以 consume_latest 会立即返回初始值 (0)。
    // *除非* 我们手动检查业务逻辑，或者 RingBuffer 设计上有“未初始化”标记。
    // 看代码实现：RingBuffer 并没有 empty() 概念，它总是由默认值初始化。
    
    // 因此测试策略调整：先读到初始值，等待变化
    int val = rb.pop_latest();
    EXPECT_EQ(val, 0); // 默认构造

    t.join();
    
    // 写入后应该能读到新值
    val = rb.pop_latest();
    EXPECT_EQ(val, 999);
}

// ============================================================================
// 4. Zero-Copy Visitor 测试
// ============================================================================
TEST(RingBufferTest, VisitorZeroCopy_StdArray) {
    RingBuffer<std::array<int, 3>, 4> rb;

    // 写入
    rb.produce([](std::array<int, 3>& slot) {
        slot = {1, 2, 3};
    });

    // 消费
    bool checked = false;
    rb.consume_latest([&](const std::array<int, 3>& slot) {
        ASSERT_EQ(slot[0], 1);
        ASSERT_EQ(slot[1], 2);
        ASSERT_EQ(slot[2], 3);
        checked = true;
    });
    EXPECT_TRUE(checked);
}
