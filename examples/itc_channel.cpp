#include "eph_channel/channel.hpp"
#include <iostream>
#include <thread>
#include <chrono>

using namespace eph::itc;

struct Message {
  int id;
  double value;
};

// 接收 Sender 的所有权
void run_sender(Sender<Message> sender) {
  std::cout << "[Sender]   Thread started. Sending 10 messages..." << std::endl;

  for (int i = 0; i < 10; ++i) {
    Message msg{i, i * 1.5};
    sender.send(msg);
    std::cout << "[Sender]   Sent: id=" << msg.id << ", value=" << msg.value << std::endl;
    // 稍微慢一点，方便观察
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "[Sender]   Done. Exiting." << std::endl;
}

// 接收 Receiver 的所有权
void run_receiver(Receiver<Message> receiver) {
  std::cout << "[Receiver] Thread started. Waiting for messages..." << std::endl;

  for (int i = 0; i < 10; ++i) {
    auto msg = receiver.receive();
    std::cout << "[Receiver] Received: id=" << msg.id << ", value=" << msg.value << std::endl;
  }
  std::cout << "[Receiver] Done. Exiting." << std::endl;
}

int main() {
  std::cout << "=== Simple ITC Channel Demo ===" << std::endl;

  // 1. 创建基于堆内存（shared_ptr）的通道
  auto [sender, receiver] = channel<Message>();

  // 2. 启动接收者线程（移动 receiver 所有权）
  std::thread receiver_thread([rx = std::move(receiver)]() mutable {
    run_receiver(std::move(rx));
  });

  // 3. 在主线程运行发送者（移动 sender 所有权）
  run_sender(std::move(sender));

  // 4. 等待线程结束
  if (receiver_thread.joinable()) {
    receiver_thread.join();
  }

  std::cout << "=== Demo Finished ===" << std::endl;
  return 0;
}
