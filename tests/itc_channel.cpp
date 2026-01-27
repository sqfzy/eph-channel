#include "eph_channel/channel.hpp"
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>

using namespace eph::itc;

// ITC Channel 测试不需要特殊的 Setup/Teardown，
// 因为资源由 shared_ptr 自动管理。
class ItcChannelTest : public ::testing::Test {};

TEST_F(ItcChannelTest, Create) {
  auto [sender, receiver] = channel<int>();

  EXPECT_EQ(sender.capacity(), eph::config::DEFAULT_CAPACITY);
  EXPECT_TRUE(receiver.is_empty());
  EXPECT_EQ(sender.size(), 0);
}

TEST_F(ItcChannelTest, BlockingSendReceive) {
  auto [sender, receiver] = channel<int, 8>();

  // 阻塞发送
  sender.send(42);
  EXPECT_FALSE(receiver.is_empty());
  EXPECT_EQ(receiver.size(), 1);

  // 阻塞接收 (值返回)
  int val = receiver.receive();
  EXPECT_EQ(val, 42);
  EXPECT_TRUE(receiver.is_empty());
}

TEST_F(ItcChannelTest, TrySendReceive) {
  // 使用小容量 channel 测试满状态
  auto [sender, receiver] = channel<int, 4>();

  // 填满: 1, 2, 3, 4
  EXPECT_TRUE(sender.try_send(1));
  EXPECT_TRUE(sender.try_send(2));
  EXPECT_TRUE(sender.try_send(3));
  EXPECT_TRUE(sender.try_send(4));

  // 再次发送应失败 (队列满)
  EXPECT_FALSE(sender.try_send(5));
  EXPECT_TRUE(sender.is_full());

  // 测试 1: try_receive(T& out)
  int val = 0;
  EXPECT_TRUE(receiver.try_receive(val));
  EXPECT_EQ(val, 1); // FIFO

  // 测试 2: try_receive() -> optional
  auto opt = receiver.try_receive();
  ASSERT_TRUE(opt.has_value());
  EXPECT_EQ(*opt, 2);

  // 还可以继续接收 2 个
  EXPECT_FALSE(receiver.is_empty());
}

TEST_F(ItcChannelTest, TimeoutOperations) {
  auto [sender, receiver] = channel<int, 2>();

  // 填满 buffer
  EXPECT_TRUE(sender.try_send(1));
  EXPECT_TRUE(sender.try_send(2));

  // 1. 测试发送超时
  auto start = std::chrono::steady_clock::now();
  bool sent = sender.send(3, std::chrono::milliseconds(50));
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(sent);
  EXPECT_GE(end - start, std::chrono::milliseconds(50));

  // 2. 测试接收超时 (先清空)
  receiver.receive();
  receiver.receive();
  
  start = std::chrono::steady_clock::now();
  int val;
  bool received = receiver.receive(val, std::chrono::milliseconds(50));
  end = std::chrono::steady_clock::now();

  EXPECT_FALSE(received);
  EXPECT_GE(end - start, std::chrono::milliseconds(50));
}

TEST_F(ItcChannelTest, BatchOperations) {
  auto [sender, receiver] = channel<int, 8>();
  std::vector<int> data = {1, 2, 3, 4, 5};

  // 批量发送
  size_t sent_count = sender.send_batch(data.begin(), data.end());
  EXPECT_EQ(sent_count, 5);

  // 批量接收
  std::vector<int> out_data(5);
  size_t recv_count = receiver.receive_batch(out_data.begin(), 5);
  EXPECT_EQ(recv_count, 5);
  EXPECT_EQ(out_data, data);
}

TEST_F(ItcChannelTest, SimpleConcurrency) {
  auto [sender, receiver] = channel<int, 1024>();
  const int count = 5000;

  // 消费者线程 (移动所有权)
  std::thread consumer([rx = std::move(receiver)]() mutable {
    for (int i = 0; i < count; ++i) {
      int val = rx.receive(); // 阻塞接收
      EXPECT_EQ(val, i);
    }
  });

  // 生产者线程 (移动所有权)
  std::thread producer([tx = std::move(sender)]() mutable {
    for (int i = 0; i < count; ++i) {
      tx.send(i); // 阻塞发送
    }
  });

  producer.join();
  consumer.join();
}
