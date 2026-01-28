#include "eph_channel/channel.hpp"
#include "../fixtures/config.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace eph::itc;
using namespace eph::test;

class ItcChannelTest : public ::testing::Test {
protected:
  TestDataGenerator gen_;
};

TEST_F(ItcChannelTest, ChannelCreation) {
  auto [sender, receiver] = channel<int>();
  
  EXPECT_EQ(sender.capacity(), eph::config::DEFAULT_CAPACITY);
  EXPECT_TRUE(receiver.is_empty());
  EXPECT_EQ(sender.size(), 0);
}

TEST_F(ItcChannelTest, SingleThreadCommunication) {
  auto [sender, receiver] = channel<TestMessage, 8>();
  
  auto msg = gen_.generate_message(42);
  sender.send(msg);
  
  EXPECT_FALSE(receiver.is_empty());
  EXPECT_EQ(receiver.size(), 1);
  
  auto received = receiver.receive();
  EXPECT_EQ(received.id, msg.id);
  EXPECT_EQ(received.timestamp, msg.timestamp);
  EXPECT_TRUE(receiver.is_empty());
}

TEST_F(ItcChannelTest, MultiThreadCommunication) {
  auto [sender, receiver] = channel<int, 1024>();
  constexpr int COUNT = TestConfig::MEDIUM_DATA_SIZE;
  
  std::thread consumer([rx = std::move(receiver)]() mutable {
    for (int i = 0; i < COUNT; ++i) {
      int val = rx.receive();
      EXPECT_EQ(val, i);
    }
  });
  
  std::thread producer([tx = std::move(sender)]() mutable {
    for (int i = 0; i < COUNT; ++i) {
      tx.send(i);
    }
  });
  
  producer.join();
  consumer.join();
}

TEST_F(ItcChannelTest, BackpressureHandling) {
  auto [sender, receiver] = channel<int, 2>();
  
  // 填满队列
  EXPECT_TRUE(sender.try_send(1));
  EXPECT_TRUE(sender.try_send(2));
  
  EXPECT_TRUE(sender.is_full());
  EXPECT_FALSE(sender.try_send(3)); // 应失败
}

TEST_F(ItcChannelTest, TimeoutMechanism) {
  auto [sender, receiver] = channel<int, 2>();
  
  // 填满
  sender.send(1);
  sender.send(2);
  
  // 发送超时
  auto start = std::chrono::steady_clock::now();
  bool sent = sender.send(3, TestConfig::SHORT_TIMEOUT);
  auto elapsed = std::chrono::steady_clock::now() - start;
  
  EXPECT_FALSE(sent);
  EXPECT_GE(elapsed, TestConfig::SHORT_TIMEOUT);
  
  // 接收超时（清空后测试）
  receiver.receive();
  receiver.receive();
  
  start = std::chrono::steady_clock::now();
  int val;
  bool received = receiver.receive(val, TestConfig::SHORT_TIMEOUT);
  elapsed = std::chrono::steady_clock::now() - start;
  
  EXPECT_FALSE(received);
  EXPECT_GE(elapsed, TestConfig::SHORT_TIMEOUT);
}

TEST_F(ItcChannelTest, BatchOperations) {
  auto [sender, receiver] = channel<int, 64>();
  
  std::vector<int> input(50);
  std::iota(input.begin(), input.end(), 0);
  
  // 批量发送
  size_t sent = sender.send_batch(input.begin(), input.end());
  EXPECT_EQ(sent, 50);
  
  // 批量接收
  std::vector<int> output(50);
  size_t received = receiver.receive_batch(output.begin(), 50);
  EXPECT_EQ(received, 50);
  EXPECT_EQ(input, output);
}

TEST_F(ItcChannelTest, PartialBatchSend) {
  auto [sender, receiver] = channel<int, 4>();
  
  std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8};
  
  // 只能发送 4 个
  size_t sent = sender.send_batch(data.begin(), data.end());
  EXPECT_EQ(sent, 4);
  
  EXPECT_TRUE(sender.is_full());
}
