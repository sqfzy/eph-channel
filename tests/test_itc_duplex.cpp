#include "shm_channel/duplex_channel.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace shm::itc;

class ItcDuplexTest : public ::testing::Test {};

struct TestRequest {
  int id;
  int val;
};

TEST_F(ItcDuplexTest, BasicSendReceive) {
  auto [client, server] = duplex_channel<TestRequest, 4>();

  // 模拟 Server 线程
  std::thread server_thread([srv = std::move(server)]() mutable {
    // 处理一个请求
    srv.receive_send([](const TestRequest &req) {
      return TestRequest{req.id, req.val * 2};
    });
  });

  // Client 发送
  TestRequest req{100, 5};
  auto resp = client.send_receive(req);

  EXPECT_EQ(resp.id, 100);
  EXPECT_EQ(resp.val, 10); // 5 * 2

  server_thread.join();
}

TEST_F(ItcDuplexTest, TimeoutRpc) {
  auto [client, server] = duplex_channel<int>();

  // Client 发起请求，设置超时，无 Server 处理
  auto start = std::chrono::steady_clock::now();
  auto result = client.send_receive(42, std::chrono::milliseconds(50));
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(result.has_value());
  EXPECT_GE(end - start, std::chrono::milliseconds(50));
}

TEST_F(ItcDuplexTest, DecoupledAsyncRpc) {
  auto [client, server] = duplex_channel<int>();

  // 1. Client 手动发送请求
  client.send_request(999);

  // 2. Server 手动接收请求
  int req = server.receive_request();
  EXPECT_EQ(req, 999);

  // 3. Server 手动发送响应
  server.send_response(req * 2);

  // 4. Client 手动接收响应
  int resp = client.receive_response();
  EXPECT_EQ(resp, 1998);
}

TEST_F(ItcDuplexTest, TrySendReceive) {
  auto [client, server] = duplex_channel<int, 2>();

  // 模拟 Server 处理函数
  auto handler = [](const int &req) { return req + 1; };

  // 1. Server 尝试接收，应失败
  EXPECT_FALSE(server.try_receive_send(handler));

  // 2. Client 异步发送请求
  client.send_request(10);

  // 3. Server 再次尝试，应该成功处理
  bool processed = server.try_receive_send(handler);
  EXPECT_TRUE(processed);

  // 4. Client 检查是否有响应
  auto resp = client.try_receive_response();
  ASSERT_TRUE(resp.has_value());
  EXPECT_EQ(*resp, 11);
}

TEST_F(ItcDuplexTest, ConcurrencyPingPong) {
  auto [client, server] = duplex_channel<int>();
  const int count = 1000;

  std::thread server_thread([srv = std::move(server)]() mutable {
    for (int i = 0; i < count; ++i) {
      srv.receive_send([](const int &req) {
        return req; // Echo
      });
    }
  });

  std::thread client_thread([cli = std::move(client)]() mutable {
    for (int i = 0; i < count; ++i) {
      int resp = cli.send_receive(i);
      EXPECT_EQ(resp, i);
    }
  });

  client_thread.join();
  server_thread.join();
}
