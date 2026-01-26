#include "shm_channel/duplex_channel.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace shm::itc;

struct Request {
  int x;
  int y;
};

using Response = Request;

// -----------------------------------------------------------
// Thread A: Client -> 发起计算请求 (x + y)
// Thread B: Server -> 接收请求，计算结果，返回
// -----------------------------------------------------------

void run_client(DuplexSender<Request> client) {
  std::cout << "[Client]   Thread started. Sending 5 tasks..." << std::endl;

  for (int i = 0; i < 5; ++i) {
    Request req{i, i * 10};

    // 发送并阻塞等待回复
    auto resp = client.send_receive(req);

    std::cout << "[Client]   Request: " << req.x << " + " << req.y
              << " | Result: " << resp.x << std::endl;

    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  std::cout << "[Client]   Done." << std::endl;
}

void run_server(DuplexReceiver<Request> server) {
  std::cout << "[Server]   Thread started." << std::endl;

  // 处理循环
  for (int i = 0; i < 5; ++i) {
    server.receive_send([](const Request &req) -> Response {
      // 模拟计算逻辑
      Response resp;
      resp.x = req.x + req.y; // 计算和
      resp.y = 0;             // 忽略
      return resp;
    });
  }
  std::cout << "[Server]   Processed all tasks. Exiting." << std::endl;
}

int main() {
  std::cout << "=== Duplex ITC Channel Demo (Threads) ===" << std::endl;

  // 1. 创建双工通道
  auto [client, server] = duplex_channel<Request>();

  // 2. 启动服务器线程
  std::thread server_thread([srv = std::move(server)]() mutable {
    run_server(std::move(srv));
  });

  // 3. 在主线程运行客户端
  run_client(std::move(client));

  // 4. 等待结束
  if (server_thread.joinable()) {
    server_thread.join();
  }

  std::cout << "=== Demo Finished ===" << std::endl;
  return 0;
}
