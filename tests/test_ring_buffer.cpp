#include "shm_channel/ring_buffer.hpp"
#include <gtest/gtest.h>

using namespace shm;

class RingBufferTest : public ::testing::Test {
protected:
  // 使用容量为 4 的 buffer 方便测试边界和回绕
  RingBuffer<int, 4> rb;
};

TEST_F(RingBufferTest, Initialization) {
  EXPECT_TRUE(rb.empty());
  EXPECT_FALSE(rb.full());
  EXPECT_EQ(rb.size(), 0);
  EXPECT_EQ(rb.capacity(), 4);
}

TEST_F(RingBufferTest, BlockingPushAndPop) {
  // 阻塞式 push (返回 void)
  rb.push(1); 
  EXPECT_EQ(rb.size(), 1);
  EXPECT_FALSE(rb.empty());

  // 阻塞式 pop (返回 void)
  int val = 0;
  rb.pop(val);
  EXPECT_EQ(val, 1);
  EXPECT_EQ(rb.size(), 0);
  EXPECT_TRUE(rb.empty());
}

TEST_F(RingBufferTest, FullState) {
  // 使用 try_push 填满队列
  EXPECT_TRUE(rb.try_push(1));
  EXPECT_TRUE(rb.try_push(2));
  EXPECT_TRUE(rb.try_push(3));
  EXPECT_TRUE(rb.try_push(4));

  EXPECT_TRUE(rb.full());
  EXPECT_EQ(rb.size(), 4);

  // 再次 push 应失败 (非阻塞，无重试)
  // 注意：这里绝对不能调用 rb.push(5)，否则会导致单线程死锁
  EXPECT_FALSE(rb.try_push(5)); 
}

TEST_F(RingBufferTest, WrapAround) {
  // 1. 填满
  for (int i = 0; i < 4; ++i) {
      EXPECT_TRUE(rb.try_push(i));
  }

  // 2. 取出两个 (0, 1)
  int val;
  rb.pop(val); EXPECT_EQ(val, 0);
  rb.pop(val); EXPECT_EQ(val, 1);

  EXPECT_EQ(rb.size(), 2);

  // 3. 再放入两个 (10, 11) —— 此时 tail 应该发生回绕
  EXPECT_TRUE(rb.try_push(10));
  EXPECT_TRUE(rb.try_push(11));

  EXPECT_TRUE(rb.full());

  // 4. 验证 FIFO 顺序: 2, 3, 10, 11
  int expected[] = {2, 3, 10, 11};
  for (int e : expected) {
    int v;
    rb.pop(v);
    EXPECT_EQ(v, e);
  }
}

TEST_F(RingBufferTest, TryPopOverloads) {
    // 准备数据
    EXPECT_TRUE(rb.try_push(100));
    EXPECT_TRUE(rb.try_push(200));

    // 测试 1: try_pop(T& out) —— 引用版本
    int val = 0;
    EXPECT_TRUE(rb.try_pop(val));
    EXPECT_EQ(val, 100);

    // 测试 2: try_pop() —— Optional 版本
    auto opt = rb.try_pop();
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ(*opt, 200);

    // 测试 3: 空队列
    EXPECT_FALSE(rb.try_pop(val));
    EXPECT_FALSE(rb.try_pop().has_value());
}

TEST_F(RingBufferTest, ZeroCopyProduceConsume) {
  struct BigData {
    int id;
    char pad[100]; // 模拟大对象
  };
  // 注意：BigData 是 Trivial 的，满足 ShmData 约束
  RingBuffer<BigData, 4> rb_big;

  // Zero-Copy Produce: 直接在 SHM 内存构造
  bool produced = rb_big.try_produce([](BigData& slot) {
    slot.id = 999;
  });
  EXPECT_TRUE(produced);

  // Zero-Copy Consume: 直接读取 SHM 内存
  bool consumed = rb_big.try_consume([](const BigData& slot) {
    EXPECT_EQ(slot.id, 999);
  });
  EXPECT_TRUE(consumed);
}
