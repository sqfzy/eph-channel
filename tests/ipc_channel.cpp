#include "eph_channel/channel.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace eph;
using namespace eph::ipc;

class ChannelTest : public ::testing::Test {
protected:
  // 使用唯一的 SHM 名称避免冲突
  std::string shm_name = "/test_shm_ipc_channel_unit_test";

  void TearDown() override {
    // 强制清理，防止测试失败残留导致后续测试异常
    shm_unlink(shm_name.c_str());
  }
};

TEST_F(ChannelTest, CreateAndConnect) {
  // Sender 创建共享内存 (Owner)
  Sender<int> sender(shm_name);
  // Receiver 连接共享内存 (User)
  Receiver<int> receiver(shm_name);

  EXPECT_EQ(sender.name(), shm_name);
  EXPECT_EQ(receiver.name(), shm_name);
  EXPECT_EQ(sender.capacity(), config::DEFAULT_CAPACITY);
}

TEST_F(ChannelTest, BlockingSendReceive) {
  auto [sender, receiver] = channel<int, 8>(shm_name);

  // 阻塞发送
  sender.send(42);
  EXPECT_FALSE(receiver.is_empty());

  // 阻塞接收 (值返回)
  int val = receiver.receive();
  EXPECT_EQ(val, 42);
  EXPECT_TRUE(receiver.is_empty());
}

TEST_F(ChannelTest, TrySendReceive) {
  // 使用小容量 channel 测试满状态
  auto [sender, receiver] = channel<int, 4>(shm_name);

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

TEST_F(ChannelTest, TimeoutOperations) {
  auto [sender, receiver] = channel<int, 2>(shm_name);

  // 填满 buffer
  EXPECT_TRUE(sender.try_send(1));
  EXPECT_TRUE(sender.try_send(2));

  // 1. 测试发送超时 (Buffer Full)
  auto start = std::chrono::steady_clock::now();
  // 尝试发送，应该阻塞直到超时
  bool sent = sender.send(3, std::chrono::milliseconds(50));
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(sent);
  EXPECT_GE(end - start, std::chrono::milliseconds(50));

  // 清空 buffer
  receiver.receive();
  receiver.receive();

  // 2. 测试接收超时 (Buffer Empty)
  start = std::chrono::steady_clock::now();
  int val;
  bool received = receiver.receive(val, std::chrono::milliseconds(50));
  end = std::chrono::steady_clock::now();

  EXPECT_FALSE(received);
  EXPECT_GE(end - start, std::chrono::milliseconds(50));
}

TEST_F(ChannelTest, BatchOperations) {
  auto [sender, receiver] = channel<int, 8>(shm_name);
  std::vector<int> data = {1, 2, 3, 4, 5};

  // 1. 批量发送
  size_t sent_count = sender.send_batch(data.begin(), data.end());
  EXPECT_EQ(sent_count, 5);
  EXPECT_EQ(sender.size(), 5);

  // 2. 批量接收
  std::vector<int> out_data(5);
  size_t recv_count = receiver.receive_batch(out_data.begin(), 5);
  EXPECT_EQ(recv_count, 5);
  EXPECT_EQ(out_data, data);
  EXPECT_TRUE(receiver.is_empty());

  // 3. 测试 Buffer 空间不足时的批量发送
  auto [s_small, r_small] = channel<int, 2>(shm_name + "_small");
  size_t sent_small = s_small.send_batch(data.begin(), data.end());
  EXPECT_EQ(sent_small, 2); // 只能发 2 个
  EXPECT_TRUE(s_small.is_full());
}

TEST_F(ChannelTest, SimpleConcurrency) {
  auto [sender, receiver] = channel<int, 1024>(shm_name);
  const int count = 5000;

  // 生产者线程
  std::thread producer([&]() {
    for (int i = 0; i < count; ++i) {
      sender.send(i); // 阻塞发送
    }
  });

  // 消费者线程
  std::thread consumer([&]() {
    for (int i = 0; i < count; ++i) {
      int val = receiver.receive(); // 阻塞接收
      EXPECT_EQ(val, i);
    }
  });

  producer.join();
  consumer.join();

  EXPECT_TRUE(receiver.is_empty());
}
