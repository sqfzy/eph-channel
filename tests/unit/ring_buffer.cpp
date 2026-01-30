#include "eph/core/ring_buffer.hpp"
#include "../fixtures/config.hpp"
#include "../fixtures/utils.hpp"
#include <gtest/gtest.h>

using namespace eph;
using namespace eph::test;


class RingBufferTest : public ::testing::Test {
protected:
  RingBuffer<int, 4> rb_;
  TestDataGenerator gen_;
};

TEST_F(RingBufferTest, Initialization) {
  EXPECT_TRUE(rb_.empty());
  EXPECT_FALSE(rb_.full());
  EXPECT_EQ(rb_.size(), 0);
  EXPECT_EQ(rb_.capacity(), 4);
}

TEST_F(RingBufferTest, BasicPush) {
  EXPECT_TRUE(rb_.try_push(1));
  EXPECT_EQ(rb_.size(), 1);
  EXPECT_FALSE(rb_.empty());
  EXPECT_FALSE(rb_.full());
}

TEST_F(RingBufferTest, BasicPop) {
  rb_.try_push(42);
  
  int val = 0;
  EXPECT_TRUE(rb_.try_pop(val));
  EXPECT_EQ(val, 42);
  EXPECT_EQ(rb_.size(), 0);
  EXPECT_TRUE(rb_.empty());
}

TEST_F(RingBufferTest, FifoOrder) {
  std::vector<int> input = {10, 20, 30, 40};
  
  for (int val : input) {
    EXPECT_TRUE(rb_.try_push(val));
  }
  
  for (int expected : input) {
    int actual;
    EXPECT_TRUE(rb_.try_pop(actual));
    EXPECT_EQ(actual, expected);
  }
  
  EXPECT_TRUE(rb_.empty());
}

TEST_F(RingBufferTest, FullState) {
  // 填满队列
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(rb_.try_push(i));
  }
  
  EXPECT_TRUE(rb_.full());
  EXPECT_EQ(rb_.size(), 4);
  
  // 再次 push 应失败
  EXPECT_FALSE(rb_.try_push(999));
  EXPECT_EQ(rb_.size(), 4); // 大小不变
}

TEST_F(RingBufferTest, EmptyState) {
  int val;
  EXPECT_FALSE(rb_.try_pop(val));
  
  auto opt = rb_.try_pop();
  EXPECT_FALSE(opt.has_value());
}

TEST_F(RingBufferTest, WrapAround) {
  // 填满
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(rb_.try_push(i));
  }
  
  // 取出两个 (0, 1)
  int val;
  rb_.try_pop(val); EXPECT_EQ(val, 0);
  rb_.try_pop(val); EXPECT_EQ(val, 1);
  
  EXPECT_EQ(rb_.size(), 2);
  
  // 再放入两个 (10, 11)
  EXPECT_TRUE(rb_.try_push(10));
  EXPECT_TRUE(rb_.try_push(11));
  
  EXPECT_TRUE(rb_.full());
  
  // 验证 FIFO: 2, 3, 10, 11
  std::vector<int> expected = {2, 3, 10, 11};
  for (int e : expected) {
    int v;
    EXPECT_TRUE(rb_.try_pop(v));
    EXPECT_EQ(v, e);
  }
  
  EXPECT_TRUE(rb_.empty());
}

TEST_F(RingBufferTest, ZeroCopyProduce) {
  bool produced = rb_.try_produce([](int& slot) {
    slot = 999;
  });
  
  EXPECT_TRUE(produced);
  EXPECT_EQ(rb_.size(), 1);
  
  int val;
  rb_.try_pop(val);
  EXPECT_EQ(val, 999);
}

TEST_F(RingBufferTest, ZeroCopyConsume) {
  rb_.try_push(777);
  
  int observed = 0;
  bool consumed = rb_.try_consume([&observed](const int& slot) {
    observed = slot;
  });
  
  EXPECT_TRUE(consumed);
  EXPECT_EQ(observed, 777);
  EXPECT_TRUE(rb_.empty());
}


class RingBufferComplexTypeTest : public ::testing::Test {
protected:
  RingBuffer<TestMessage, 8> rb_msg_;
  RingBuffer<LargeTestData, 4> rb_large_;
  TestDataGenerator gen_;
};

TEST_F(RingBufferComplexTypeTest, LargeObjectHandling) {
  auto data = gen_.generate_large_data(12345);
  
  EXPECT_TRUE(rb_large_.try_push(data));
  
  LargeTestData retrieved;
  EXPECT_TRUE(rb_large_.try_pop(retrieved));
  EXPECT_EQ(retrieved, data);
}

TEST_F(RingBufferComplexTypeTest, MemoryAlignment) {
  // 验证关键成员的对齐
  alignas(1024) RingBuffer<int, 8> rb;
  
  // 注意：这里无法直接访问私有成员，仅验证对象本身对齐
  verify_alignment(&rb, alignof(decltype(rb)));
}


TEST(RingBufferConcurrencyTest, SPSC_BasicConcurrency) {
  RingBuffer<int, 1024> rb;
  constexpr int COUNT = TestConfig::MEDIUM_DATA_SIZE;
  
  std::atomic<bool> producer_done{false};
  
  // 生产者线程
  std::thread producer([&]() {
    for (int i = 0; i < COUNT; ++i) {
      while (!rb.try_push(i)) {
        cpu_relax();
      }
    }
    producer_done = true;
  });
  
  // 消费者线程
  std::thread consumer([&]() {
    for (int i = 0; i < COUNT; ++i) {
      int val = -1;
      while (!rb.try_pop(val)) {
        cpu_relax();
      }
      EXPECT_EQ(val, i);
    }
  });
  
  producer.join();
  consumer.join();
  
  EXPECT_TRUE(producer_done);
  EXPECT_TRUE(rb.empty());
}

TEST(RingBufferBlockingTest, BlockingPushPop) {
  RingBuffer<int, 8> rb;
  
  std::thread consumer([&]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    int val;
    rb.pop(val); // 阻塞等待
    EXPECT_EQ(val, 42);
  });
  
  // 主线程发送
  rb.push(42);
  
  consumer.join();
}


TEST(RingBufferEdgeCaseTest, SingleElementCapacity) {
  RingBuffer<int, 1> rb;
  
  EXPECT_TRUE(rb.try_push(1));
  EXPECT_TRUE(rb.full());
  EXPECT_FALSE(rb.try_push(2));
  
  int val;
  EXPECT_TRUE(rb.try_pop(val));
  EXPECT_EQ(val, 1);
  EXPECT_TRUE(rb.empty());
}

TEST(RingBufferEdgeCaseTest, LargeCapacity) {
  RingBuffer<int, 8192> rb;
  
  EXPECT_EQ(rb.capacity(), 8192);
  
  // 填充一半
  for (int i = 0; i < 4096; ++i) {
    EXPECT_TRUE(rb.try_push(i));
  }
  
  EXPECT_EQ(rb.size(), 4096);
  EXPECT_FALSE(rb.full());
}
