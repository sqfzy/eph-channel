#include "eph_channel/channel.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace eph;
using namespace eph::udp;

class UdpChannelTest : public ::testing::Test {
protected:
  // 使用原子计数器为每个测试分配唯一的端口，避免 TIME_WAIT 或冲突
  static inline std::atomic<uint16_t> port_counter{20000};
  uint16_t port;
  const std::string localhost = "127.0.0.1";

  void SetUp() override { port = port_counter++; }
};

TEST_F(UdpChannelTest, CreateAndBind) {
  // 显式创建
  Receiver<int> receiver(port);
  Sender<int> sender(localhost, port);

  // UDP 无法精确获取内核队列大小，通常返回 0
  EXPECT_EQ(receiver.size(), 0);
  EXPECT_EQ(sender.size(), 0);

  // UDP 发送端在用户态很难判断 Full
  EXPECT_FALSE(sender.is_full());
}

TEST_F(UdpChannelTest, BlockingSendReceive) {
  // 1. 先创建 Receiver 绑定端口
  Receiver<int> receiver(port);
  // 2. 再创建 Sender 连接目标
  Sender<int> sender(localhost, port);

  // 发送数据
  sender.send(42);

  // 接收数据
  int val = receiver.receive();
  EXPECT_EQ(val, 42);
}

TEST_F(UdpChannelTest, TrySendReceive) {
  Receiver<int> receiver(port);
  Sender<int> sender(localhost, port);

  // UDP try_send 通常都会成功
  EXPECT_TRUE(sender.try_send(1));
  EXPECT_TRUE(sender.try_send(2));

  // 测试 1: try_receive(T& out)
  int val = 0;
  // 给一点时间让内核传递数据
  std::this_thread::sleep_for(std::chrono::milliseconds(1));

  EXPECT_TRUE(receiver.try_receive(val));
  EXPECT_EQ(val, 1);

  // 测试 2: try_receive() -> optional
  auto opt = receiver.try_receive();
  ASSERT_TRUE(opt.has_value());
  EXPECT_EQ(*opt, 2);

  // 此时应该空了
  EXPECT_FALSE(receiver.try_receive(val));
}

TEST_F(UdpChannelTest, ReceiveTimeout) {
  Receiver<int> receiver(port);

  // 测试接收超时 (此时没有 Sender，Buffer Empty)
  auto start = std::chrono::steady_clock::now();
  int val;
  bool received = receiver.receive(val, std::chrono::milliseconds(50));
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(received);
  EXPECT_GE(end - start, std::chrono::milliseconds(50));
}

TEST_F(UdpChannelTest, BatchOperations) {
  Receiver<int> receiver(port);
  Sender<int> sender(localhost, port);

  std::vector<int> data = {10, 20, 30, 40, 50};

  // 1. 批量发送
  size_t sent_count = sender.send_batch(data.begin(), data.end());
  EXPECT_EQ(sent_count, 5);

  // 稍微等待内核处理
  std::this_thread::sleep_for(std::chrono::milliseconds(5));

  // 2. 批量接收
  std::vector<int> out_data(5);
  size_t recv_count = receiver.receive_batch(out_data.begin(), 5);

  EXPECT_EQ(recv_count, 5);
  EXPECT_EQ(out_data, data);
}

TEST_F(UdpChannelTest, SimpleConcurrency) {
  // 设置一个较大的 Capacity 以增大 SO_RCVBUF
  // 注意：Receiver 必须先构造以绑定端口
  Receiver<int, 4096> receiver(port);
  Sender<int, 4096> sender(localhost, port);

  const int count = 1000;

  // 消费者线程 (移动所有权)
  std::thread consumer([rx = std::move(receiver)]() mutable {
    for (int i = 0; i < count; ++i) {
      int val = rx.receive();
      EXPECT_EQ(val, i);
    }
  });

  // 生产者线程 (移动所有权)
  std::thread producer([tx = std::move(sender)]() mutable {
    // 稍微延迟启动生产者，确保消费者线程已经跑起来
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    for (int i = 0; i < count; ++i) {
      tx.send(i);
      if (i % 100 == 0)
        std::this_thread::yield();
    }
  });

  producer.join();
  consumer.join();
}
