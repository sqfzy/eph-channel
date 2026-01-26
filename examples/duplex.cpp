#include "shm_channel/duplex_channel.hpp"
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

struct Request {
  int x;
  int y;
};

using Response = Request;

// -----------------------------------------------------------
// Process A (Parent): Client (Owner) -> 发起计算请求 (x + y)
// Process B (Child):  Server (User)  -> 接收请求，计算结果，返回
// -----------------------------------------------------------

void run_client_process() {
  // Client 作为 Owner 创建共享内存
  shm::DuplexSender<Request> client("/demo_rpc");
  std::cout << "[Client]   Launched. Waiting for server to join..."
            << std::endl;

  // 简单的握手，确保对面准备好了
  client.handshake();

  std::cout << "[Client]   Server ready. Sending 5 tasks..." << std::endl;

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

void run_server_process() {
  std::this_thread::sleep_for(
      std::chrono::milliseconds(100)); // 等待 Client 创建内存

  try {
    shm::DuplexReceiver<Request> server("/demo_rpc");
    std::cout << "[Server]   Connected." << std::endl;

    // 响应握手
    server.handshake();

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
  } catch (const std::exception &e) {
    std::cerr << "[Server]   Error: " << e.what() << std::endl;
  }
}

int main() {
  std::cout << "=== Duplex RPC Channel Demo (Auto-Fork) ===" << std::endl;

  pid_t pid = fork();

  if (pid < 0) {
    return 1;
  }

  if (pid == 0) {
    // 子进程运行 Server (响应端)
    run_server_process();
  } else {
    // 父进程运行 Client (请求端)
    run_client_process();
    wait(NULL); // 等待子进程
    std::cout << "=== Demo Finished ===" << std::endl;
  }

  return 0;
}
