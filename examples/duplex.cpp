#include "shm_channel/duplex_channel.hpp"
#include <iostream>
#include <thread>

struct Request {
  int x;
  int y;
};

struct Response {
  int sum;
};

// 为了简化，这里使用相同的类型
using Message = Request;

void client_example() {
  // 创建 DuplexSender
  shm::DuplexSender<Message> client("/rpc");

  std::cout << "[Client] Waiting for server..." << std::endl;
  client.handshake();

  std::cout << "[Client] Sending requests..." << std::endl;

  for (int i = 0; i < 5; ++i) {
    Message req{i, i * 2};
    std::cout << "  Request: " << req.x << " + " << req.y << std::endl;

    auto resp = client.send_receive(req);
    std::cout << "  Response: sum = " << resp.x << std::endl;
  }

  std::cout << "[Client] Done." << std::endl;
}

void server_example() {
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 创建 DuplexReceiver
  shm::DuplexReceiver<Message> server("/rpc");

  std::cout << "[Server] Ready." << std::endl;
  server.handshake();

  std::cout << "[Server] Processing requests..." << std::endl;

  for (int i = 0; i < 5; ++i) {
    server.receive_send([](const Message &req) -> Message {
      Message resp;
      resp.x = req.x + req.y;
      resp.y = 0;
      std::cout << "  Computing: " << req.x << " + " << req.y << " = "
                << resp.x << std::endl;
      return resp;
    });
  }

  std::cout << "[Server] Done." << std::endl;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " [client|server]" << std::endl;
    return 1;
  }

  std::string mode = argv[1];
  if (mode == "client")
    client_example();
  else if (mode == "server")
    server_example();
  else
    std::cerr << "Unknown mode: " << mode << std::endl;

  return 0;
}
