#include <atomic>
#include <gtest/gtest.h>
#include <thread>

#include "eph/core/queue.hpp"

using namespace eph;

// 用于测试的简单 POD 类型 (满足 ShmData Concept)
struct Point {
  int x;
  int y;

  // 重载等于号方便 gtest 比较
  bool operator==(const Point &other) const {
    return x == other.x && y == other.y;
  }
};

// ============================================================================
// 1. 基础功能测试：边界条件与回绕
// ============================================================================
TEST(BoundedQueueTest, BasicFlowAndWrapAround) {
  // 使用极小的容量 (4) 以便快速触发回绕和 Full 状态
  BoundedQueue<int, 4> q;

  ASSERT_TRUE(q.empty());
  ASSERT_FALSE(q.full());
  ASSERT_EQ(q.size(), 0);

  // 1. 填满队列
  EXPECT_TRUE(q.try_push(1));
  EXPECT_TRUE(q.try_push(2));
  EXPECT_TRUE(q.try_push(3));
  EXPECT_TRUE(q.try_push(4));

  // 此时应已满
  EXPECT_FALSE(q.try_push(5));
  EXPECT_TRUE(q.full());
  EXPECT_EQ(q.size(), 4);

  // 2. 消费一半
  int val;
  EXPECT_TRUE(q.try_pop(val));
  ASSERT_EQ(val, 1);
  EXPECT_TRUE(q.try_pop(val));
  ASSERT_EQ(val, 2);

  EXPECT_EQ(q.size(), 2);
  EXPECT_FALSE(q.full());

  // 3. 继续写入 (触发 tail 回绕/掩码逻辑)
  EXPECT_TRUE(q.try_push(5));
  EXPECT_TRUE(q.try_push(6));

  EXPECT_TRUE(q.full());

  // 4. 全部读出 (触发 head 回绕/影子索引更新)
  // 此时队列里应该是: 3, 4, 5, 6
  EXPECT_TRUE(q.try_pop(val));
  ASSERT_EQ(val, 3);
  EXPECT_TRUE(q.try_pop(val));
  ASSERT_EQ(val, 4);
  EXPECT_TRUE(q.try_pop(val));
  ASSERT_EQ(val, 5);
  EXPECT_TRUE(q.try_pop(val));
  ASSERT_EQ(val, 6);

  EXPECT_TRUE(q.empty());
  EXPECT_FALSE(q.try_pop(val));
}

// ============================================================================
// 2. 零拷贝/Visitor API 测试
// ============================================================================
TEST(BoundedQueueTest, ZeroCopyVisitorSemantics) {
  BoundedQueue<Point, 8> q;

  // 测试 try_produce (Emplace 语义)
  bool pushed = q.try_produce([](Point &slot) {
    slot.x = 10;
    slot.y = 20;
  });
  ASSERT_TRUE(pushed);

  // 测试 try_consume (读取引用)
  bool consumed = q.try_consume([](Point &slot) {
    EXPECT_EQ(slot.x, 10);
    EXPECT_EQ(slot.y, 20);

    // 甚至可以修改 (如果是 move 语义) 或读取后重置
    slot.x = 0;
  });
  ASSERT_TRUE(consumed);

  // 再次读取应失败 (已空)
  EXPECT_FALSE(q.try_consume([](Point &) {}));
}

// ============================================================================
// 3. 并发压力测试 (核心测试)
// ============================================================================
TEST(BoundedQueueTest, SPSC_ConcurrencyStress) {
  // 较大的容量，减少 CPU 自旋等待
  constexpr size_t CAPACITY = 1024;
  constexpr int ITERATIONS = 1'000'000;

  BoundedQueue<int, CAPACITY> q;

  std::atomic<bool> producer_done{false};

  // 消费者线程
  std::thread consumer([&]() {
    int expected = 0;
    int val;
    while (expected < ITERATIONS) {
      // 使用阻塞式 pop，测试 cpu_relax() 和自旋逻辑
      val = q.pop();

      if (val != expected) {
        // 如果顺序错了，说明内存序(Acquire/Release)有问题
        GTEST_FAIL() << "Order mismatch! Expected " << expected << " but got "
                     << val;
      }
      expected++;
    }
  });

  // 生产者线程 (主线程)
  for (int i = 0; i < ITERATIONS; ++i) {
    // 使用阻塞式 push
    q.push(i);
  }

  consumer.join();

  // 最终状态检查
  EXPECT_TRUE(q.empty());
}

// ============================================================================
// 4. Shadow Index 优化逻辑验证
// ============================================================================
// 这个测试旨在覆盖 try_produce 中 "快路径" 失败后 "慢路径" (加载 atomic head)
// 的逻辑
TEST(BoundedQueueTest, ShadowIndexLogic) {
  BoundedQueue<int, 4> q;

  // 1. 填满
  q.push(1);
  q.push(2);
  q.push(3);
  q.push(4);

  // 此时 producer 的 shadow_head_ 应该是 0 (或者初始值)
  // tail_ 是 4

  // 2. 消费者消费全部
  EXPECT_EQ(q.pop(), 1);
  EXPECT_EQ(q.pop(), 2);
  EXPECT_EQ(q.pop(), 3);
  EXPECT_EQ(q.pop(), 4);
  // 此时 consumer 的 head_ 是 4
  // 但 producer 的 shadow_head_ 可能还是旧的
  // (取决于实现细节，通常在满时才会更新)

  // 3. 再次写入
  // 此时 producer.tail_ (4) - producer.shadow_head_ (0) >= Capacity (4)
  // 这会触发 try_produce 中的慢路径：重新 load consumer.head_
  // 如果逻辑正确，它会发现 head_ 变成了 4，从而允许写入

  EXPECT_TRUE(q.try_push(5));

  int val;
  EXPECT_TRUE(q.try_pop(val));
  EXPECT_EQ(val, 5);
}
