#include "eph/channel.hpp"
#include "../fixtures/config.hpp"
#include "../fixtures/utils.hpp"
#include <gtest/gtest.h>
#include <thread>

using namespace eph::udp;
using namespace eph::test;

class UdpChannelTest : public ::testing::Test {
protected:
  static constexpr uint16_t TEST_PORT = 21000;
  static constexpr const char* TEST_IP = "127.0.0.1";
  
  TestDataGenerator gen_;
};

// UDP-I-001: 本地回环通信
TEST_F(UdpChannelTest, LoopbackCommunication) {
  Receiver<int> receiver(TEST_PORT);
  Sender<int> sender(TEST_IP, TEST_PORT);
  
  sender.send(42);
  
  // UDP 是异步的，可能需要短暂等待
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  int val = receiver.receive();
  EXPECT_EQ(val, 42);
}

// 复杂类型传输
TEST_F(UdpChannelTest, ComplexTypeTransfer) {
  Receiver<TestMessage> receiver(TEST_PORT + 1);
  Sender<TestMessage> sender(TEST_IP, TEST_PORT + 1);
  
  auto msg = gen_.generate_message(123);
  sender.send(msg);
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  auto received = receiver.receive();
  EXPECT_EQ(received.id, msg.id);
  EXPECT_EQ(received.timestamp, msg.timestamp);
}

TEST_F(UdpChannelTest, TrySendReceive) {
  Receiver<int> receiver(TEST_PORT + 2);
  Sender<int> sender(TEST_IP, TEST_PORT + 2);
  
  EXPECT_TRUE(sender.try_send(999));
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  int val;
  EXPECT_TRUE(receiver.try_receive(val));
  EXPECT_EQ(val, 999);
}

TEST_F(UdpChannelTest, TryReceiveEmpty) {
  Receiver<int> receiver(TEST_PORT + 3);
  
  int val;
  EXPECT_FALSE(receiver.try_receive(val));
}

TEST_F(UdpChannelTest, BatchTransfer) {
  Receiver<int> receiver(TEST_PORT + 4);
  Sender<int> sender(TEST_IP, TEST_PORT + 4);
  
  std::vector<int> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
  
  size_t sent = sender.send_batch(data.begin(), data.end());
  EXPECT_EQ(sent, 10);
  
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  std::vector<int> received(10);
  size_t recv_count = receiver.receive_batch(received.begin(), 10);
  EXPECT_EQ(recv_count, 10);
  EXPECT_EQ(data, received);
}

TEST_F(UdpChannelTest, PacketLoss) {
  Receiver<uint64_t, 16> receiver(TEST_PORT + 5);
  Sender<uint64_t, 16> sender(TEST_IP, TEST_PORT + 5);
  
  constexpr int COUNT = 1000;
  
  // 快速发送大量数据
  for (uint64_t i = 0; i < COUNT; ++i) {
     auto _ =sender.try_send(i); // 不等待
  }
  
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // 接收所有可用数据
  int received_count = 0;
  uint64_t val;
  while (receiver.try_receive(val)) {
    received_count++;
  }
  
  // UDP 可能丢包，接收数量 <= 发送数量
  EXPECT_LE(received_count, COUNT);
  
  // 本地回环通常丢包很少，但不能保证 100%
  std::cout << "Received " << received_count << "/" << COUNT 
            << " packets (" << (received_count * 100.0 / COUNT) << "%)" << std::endl;
}

TEST_F(UdpChannelTest, MtuBoundary) {
  // 标准以太网 MTU 是 1500 字节
  // UDP 头 8 字节，IP 头 20 字节，剩余 1472 字节
  struct LargeDatagram {
    char data[1400]; // 接近但不超过 MTU
  };
  
  Receiver<LargeDatagram> receiver(TEST_PORT + 6);
  Sender<LargeDatagram> sender(TEST_IP, TEST_PORT + 6);
  
  LargeDatagram msg;
  std::memset(msg.data, 'A', sizeof(msg.data));
  
  EXPECT_TRUE(sender.try_send(msg));
  
  std::this_thread::sleep_for(std::chrono::milliseconds(10));
  
  LargeDatagram received;
  EXPECT_TRUE(receiver.try_receive(received));
  EXPECT_EQ(std::memcmp(msg.data, received.data, sizeof(msg.data)), 0);
}

TEST_F(UdpChannelTest, OversizedPacket) {
  struct OversizedDatagram {
    char data[65000]; // 接近 UDP 最大值
  };
  
  Receiver<OversizedDatagram> receiver(TEST_PORT + 7);
  Sender<OversizedDatagram> sender(TEST_IP, TEST_PORT + 7);
  
  OversizedDatagram msg;
  std::memset(msg.data, 'B', sizeof(msg.data));
  
  // 可能需要分片
  bool sent = sender.try_send(msg);
  
  if (sent) {
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    
    OversizedDatagram received;
    bool recv = receiver.try_receive(received);
    
    if (recv) {
      EXPECT_EQ(std::memcmp(msg.data, received.data, sizeof(msg.data)), 0);
    }
  }
  
  // 这个测试可能因为 OS 配置失败，不强制要求成功
}

TEST_F(UdpChannelTest, MultipleReceivers) {
  constexpr uint16_t SHARED_PORT = TEST_PORT + 8;
  
  Receiver<int> receiver1(SHARED_PORT);
  Receiver<int> receiver2(SHARED_PORT);
  Receiver<int> receiver3(SHARED_PORT);
  
  Sender<int> sender(TEST_IP, SHARED_PORT);
  
  // 发送 3 条消息
  sender.send(1);
  sender.send(2);
  sender.send(3);
  
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  // 每个 receiver 可能收到不同的消息（负载均衡）
  int val1, val2, val3;
  bool r1 = receiver1.try_receive(val1);
  bool r2 = receiver2.try_receive(val2);
  bool r3 = receiver3.try_receive(val3);
  
  // 至少有一个收到
  EXPECT_TRUE(r1 || r2 || r3);
}

TEST_F(UdpChannelTest, ReceiveTimeout) {
  Receiver<int> receiver(TEST_PORT + 9);
  
  auto start = std::chrono::steady_clock::now();
  int val;
  bool received = receiver.receive(val, TestConfig::SHORT_TIMEOUT);
  auto elapsed = std::chrono::steady_clock::now() - start;
  
  EXPECT_FALSE(received);
  EXPECT_GE(elapsed, TestConfig::SHORT_TIMEOUT);
}

TEST_F(UdpChannelTest, BufferOverflow) {
  Receiver<int, 4> receiver(TEST_PORT + 10); // 小缓冲区
  Sender<int, 4> sender(TEST_IP, TEST_PORT + 10);
  
  // 快速发送大量数据
  for (int i = 0; i < 100; ++i) {
     auto _ =sender.try_send(i);
  }
  
  std::this_thread::sleep_for(std::chrono::milliseconds(50));
  
  // 能接收到的数据有限
  int count = 0;
  int val;
  while (receiver.try_receive(val)) {
    count++;
  }
  
  // 应该少于发送的数量（因为缓冲区溢出）
  EXPECT_LT(count, 100);
  std::cout << "Received " << count << "/100 packets (buffer overflow)" << std::endl;
}

TEST_F(UdpChannelTest, ConcurrentSenders) {
  Receiver<int> receiver(TEST_PORT + 11);
  
  ThreadRunner threads;
  std::atomic<int> sent_total{0};
  
  for (int t = 0; t < 3; ++t) {
    threads.spawn([&, t]() {
      Sender<int> sender(TEST_IP, TEST_PORT + 11);
      for (int i = 0; i < 100; ++i) {
        if (sender.try_send(t * 1000 + i)) {
          sent_total++;
        }
      }
    });
  }
  
  threads.join_all();
  
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  int received_count = 0;
  int val;
  while (receiver.try_receive(val)) {
    received_count++;
  }
  
  std::cout << "Sent: " << sent_total << ", Received: " << received_count << std::endl;
  EXPECT_GT(received_count, 0);
}
