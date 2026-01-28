#include "eph_channel/channel.hpp"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <iostream>

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
  Receiver<int> receiver(port);
  Sender<int> sender(localhost, port);
  EXPECT_EQ(receiver.size(), 0);
  EXPECT_EQ(sender.size(), 0);
  EXPECT_FALSE(sender.is_full());
}

TEST_F(UdpChannelTest, BlockingSendReceive) {
  Receiver<int> receiver(port);
  Sender<int> sender(localhost, port);
  sender.send(42);
  int val = receiver.receive();
  EXPECT_EQ(val, 42);
}

TEST_F(UdpChannelTest, TrySendReceive) {
  Receiver<int> receiver(port);
  Sender<int> sender(localhost, port);

  EXPECT_TRUE(sender.try_send(1));
  EXPECT_TRUE(sender.try_send(2));

  int val = 0;
  // 给内核一点时间搬运数据
  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  EXPECT_TRUE(receiver.try_receive(val));
  EXPECT_EQ(val, 1);

  auto opt = receiver.try_receive();
  ASSERT_TRUE(opt.has_value());
  EXPECT_EQ(*opt, 2);
}

TEST_F(UdpChannelTest, ReceiveTimeout) {
  Receiver<int> receiver(port);
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

  size_t sent_count = sender.send_batch(data.begin(), data.end());
  EXPECT_EQ(sent_count, 5);

  std::this_thread::sleep_for(std::chrono::milliseconds(10));

  std::vector<int> out_data(5);
  size_t recv_count = receiver.receive_batch(out_data.begin(), 5);
  EXPECT_EQ(recv_count, 5);
  EXPECT_EQ(out_data, data);
}

TEST_F(UdpChannelTest, SimpleConcurrency) {
  // 1. 增大 Capacity 以增加 SO_RCVBUF，减少 Localhost 丢包概率
  // 65536 * 4 bytes = 256KB
  constexpr size_t TEST_CAPACITY = 65536;
  Receiver<int, TEST_CAPACITY> receiver(port);
  Sender<int, TEST_CAPACITY> sender(localhost, port);

  const int count = 1000;
  const int stop_signal = -1; // 结束标志

  // 消费者线程
  std::thread consumer([rx = std::move(receiver), count]() mutable {
    int last_val = -1;
    int received_count = 0;
    int out_of_order_count = 0;
    
    while (true) {
      int val = 0;
      // 使用超时接收，防止因丢包导致的永久卡死
      if (rx.receive(val, std::chrono::milliseconds(200))) {
        
        if (val == stop_signal) {
          break; // 收到结束信号
        }

        // --- 核心修改 ---
        // UDP 不保证顺序，所以我们不再 EXPECT_GT(val, last_val)
        // 而是统计乱序情况。
        if (last_val != -1) {
            if (val <= last_val) {
                out_of_order_count++;
            }
        }
        
        // 只有当这是新的一轮(为了简单的统计逻辑)或者我们不介意乱序时更新
        // 为了统计乱序，我们总是更新 last_val，这样能捕捉到每一次“回退”
        last_val = val;
        received_count++;
      } else {
        // 超时退出
        break;
      }
    }
    
    // 打印统计信息，让你知道发生了什么
    std::cout << "[UDP Test] Total: " << count 
              << ", Received: " << received_count 
              << ", OutOfOrder: " << out_of_order_count 
              << ", LossRate: " << (1.0 - (double)received_count/count)*100.0 << "%" 
              << std::endl;

    // 验证确实收到了一些数据
    EXPECT_GT(received_count, count * 0.5) << "Too many packets lost on localhost!";
  });

  // 生产者线程
  std::thread producer([tx = std::move(sender), stop_signal]() mutable {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    for (int i = 0; i < count; ++i) {
      tx.send(i);
      // 简单的流控：每 100 个包让出 CPU，给接收端处理时间
      if (i % 100 == 0) std::this_thread::yield();
    }
    
    // 发送多个结束信号，确保 UDP 丢包情况下也能送达
    for(int k=0; k<10; ++k) {
        tx.send(stop_signal);
        std::this_thread::yield();
    }
  });

  producer.join();
  consumer.join();
}
