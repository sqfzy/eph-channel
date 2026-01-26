#include "shm_channel/duplex_channel.hpp"
#include <chrono>
#include <gtest/gtest.h>
#include <thread>

using namespace shm::ipc;

class IpcDuplexTest : public ::testing::Test {
protected:
  // 使用独立的 SHM 名称，避免与其他测试冲突
  std::string shm_name = "/test_shm_ipc_duplex_unit_test";

  void TearDown() override {
    // 确保清理共享内存
    shm_unlink(shm_name.c_str());
  }
};

TEST_F(IpcDuplexTest, CreateAndConnect) {
  // 测试 factory 函数是否正确创建了 Owner (Client) 和 User (Server)
  auto [client, server] = duplex_channel<int>(shm_name);

  // 简单的连通性检查 (利用 try_send_receive 的副作用)
  // Client 尝试发送，应该成功进入 p2c 队列
  auto res = client.try_send_receive(1);
  // 因为 Server 还没处理，c2p 为空，所以这里应该返回 nullopt
  EXPECT_FALSE(res.has_value());
}

TEST_F(IpcDuplexTest, SimpleRpcBlocking) {
  auto [client, server] = duplex_channel<int>(shm_name);

  // 模拟 Server 线程
  std::thread server_thread([&server]() {
    // 阻塞等待并处理 1 个请求
    server.receive_send([](const int &req) {
      return req * 2; // Return request * 2
    });
  });

  // Client 发送请求并阻塞等待结果
  int request = 10;
  int response = client.send_receive(request);

  EXPECT_EQ(response, 20);

  server_thread.join();
}

TEST_F(IpcDuplexTest, TimeoutRpc) {
  auto [client, server] = duplex_channel<int>(shm_name);

  // Client 发起请求，设置超时
  // 注意：没有启动 Server 线程，所以应该超时
  auto start = std::chrono::steady_clock::now();
  auto result = client.send_receive(42, std::chrono::milliseconds(100));
  auto end = std::chrono::steady_clock::now();

  EXPECT_FALSE(result.has_value());
  // 确保至少等待了指定时间
  EXPECT_GE(end - start, std::chrono::milliseconds(100));

  // 清理 buffer (因为 client 已经把请求放进去了)
  server.receive_request();
}

TEST_F(IpcDuplexTest, DecoupledAsyncRpc) {
  // 测试解耦的 Request/Response 接口
  auto [client, server] = duplex_channel<int>(shm_name);

  // 1. Client 发送请求 (无需 Server 在线)
  client.send_request(100);

  // 2. Server 稍后取出请求
  auto req_opt = server.try_receive_request();
  ASSERT_TRUE(req_opt.has_value());
  EXPECT_EQ(*req_opt, 100);

  // 3. Server 处理并发送响应
  server.send_response(200);

  // 4. Client 取出响应
  auto resp_opt = client.try_receive_response();
  ASSERT_TRUE(resp_opt.has_value());
  EXPECT_EQ(*resp_opt, 200);
}

TEST_F(IpcDuplexTest, StructDataRpc) {
  struct Point {
    int x;
    int y;
  };
  auto [client, server] = duplex_channel<Point>(shm_name);

  std::thread server_thread([&server]() {
    server.receive_send([](const Point &p) {
      return Point{p.y, p.x}; // 交换坐标
    });
  });

  Point req{10, 20};
  Point res = client.send_receive(req);

  EXPECT_EQ(res.x, 20);
  EXPECT_EQ(res.y, 10);

  server_thread.join();
}

TEST_F(IpcDuplexTest, ConcurrencyPingPong) {
  auto [client, server] = duplex_channel<uint64_t, 1024>(shm_name);
  const int count = 2000;

  std::thread server_thread([&server]() {
    for (int i = 0; i < count; ++i) {
      server.receive_send([](const uint64_t &req) {
        return req; // Echo
      });
    }
  });

  std::thread client_thread([&client]() {
    for (int i = 0; i < count; ++i) {
      uint64_t resp = client.send_receive(i);
      ASSERT_EQ(resp, i);
    }
  });

  client_thread.join();
  server_thread.join();
}
