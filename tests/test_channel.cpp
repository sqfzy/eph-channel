#include "shm_channel/channel.hpp"
#include <gtest/gtest.h>
#include <thread>

using namespace shm;

class ChannelTest : public ::testing::Test {
protected:
  std::string shm_name = "/test_shm_channel_unit_test";

  void TearDown() override {
    // 强制清理，防止测试失败残留
    shm_unlink(shm_name.c_str());
  }
};

TEST_F(ChannelTest, CreateAndConnect) {
  // Sender 创建共享内存
  Sender<int> sender(shm_name);
  // Receiver 连接共享内存
  Receiver<int> receiver(shm_name);

  EXPECT_EQ(sender.name(), shm_name);
  EXPECT_EQ(receiver.name(), shm_name);
}

TEST_F(ChannelTest, SendReceiveBasic) {
  auto [sender, receiver] = channel<int, 8>(shm_name);

  sender.send(42);
  EXPECT_FALSE(receiver.is_empty());

  int val = receiver.receive();
  EXPECT_EQ(val, 42);
  EXPECT_TRUE(receiver.is_empty());
}

TEST_F(ChannelTest, TrySendReceive) {
  auto [sender, receiver] = channel<int, 4>(shm_name);

  // 填满
  EXPECT_TRUE(sender.try_send(1));
  EXPECT_TRUE(sender.try_send(2));
  EXPECT_TRUE(sender.try_send(3));
  EXPECT_TRUE(sender.try_send(4));

  // 再次发送应失败（非阻塞）
  EXPECT_FALSE(sender.try_send(5));

  int val = 0;
  EXPECT_TRUE(receiver.try_receive(val));
  EXPECT_EQ(val, 1);
}

TEST_F(ChannelTest, SimpleConcurrency) {
  // 模拟一个简单的多线程场景（虽然实际是用在多进程，但多线程也能验证内存模型）
  auto [sender, receiver] = channel<int, 1024>(shm_name);
  const int count = 1000;

  std::thread producer([&]() {
    for (int i = 0; i < count; ++i) {
      sender.send(i);
    }
  });

  std::thread consumer([&]() {
    for (int i = 0; i < count; ++i) {
      int val = receiver.receive();
      EXPECT_EQ(val, i);
    }
  });

  producer.join();
  consumer.join();
}
