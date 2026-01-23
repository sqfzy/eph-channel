#include "shm_channel/ring_buffer.hpp"
#include <gtest/gtest.h>

using namespace shm;

// 测试夹具
class RingBufferTest : public ::testing::Test {
protected:
  // 使用较小的容量以便测试边界条件，必须是2的幂
  RingBuffer<int, 4> rb;
};

TEST_F(RingBufferTest, Initialization) {
  EXPECT_TRUE(rb.empty());
  EXPECT_FALSE(rb.full());
  EXPECT_EQ(rb.size(), 0);
  EXPECT_EQ(rb.capacity(), 4);
}

TEST_F(RingBufferTest, PushAndPop) {
  EXPECT_TRUE(rb.push(1));
  EXPECT_EQ(rb.size(), 1);
  EXPECT_FALSE(rb.empty());

  int val;
  EXPECT_TRUE(rb.pop(val));
  EXPECT_EQ(val, 1);
  EXPECT_EQ(rb.size(), 0);
  EXPECT_TRUE(rb.empty());
}

TEST_F(RingBufferTest, FullState) {
  EXPECT_TRUE(rb.push(1));
  EXPECT_TRUE(rb.push(2));
  EXPECT_TRUE(rb.push(3));
  EXPECT_TRUE(rb.push(4));

  EXPECT_TRUE(rb.full());
  EXPECT_FALSE(rb.push(5)); // 应该失败
  EXPECT_EQ(rb.size(), 4);
}

TEST_F(RingBufferTest, WrapAround) {
  // 填满
  for (int i = 0; i < 4; ++i)
    rb.push(i);

  // 取出两个
  int val;
  rb.pop(val); // 0
  rb.pop(val); // 1

  // 再放入两个（此时应该发生回绕）
  EXPECT_TRUE(rb.push(10));
  EXPECT_TRUE(rb.push(11));

  EXPECT_TRUE(rb.full());

  // 验证顺序
  int expected[] = {2, 3, 10, 11};
  for (int e : expected) {
    int v;
    EXPECT_TRUE(rb.pop(v));
    EXPECT_EQ(v, e);
  }
}
