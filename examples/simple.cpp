#include "shm_channel/channel.hpp"
#include <iostream>
#include <thread>

struct Message {
  int id;
  double value;
};

void sender_example() {
  // 创建 Sender（自动创建共享内存）
  shm::Sender<Message> sender("/demo");

  std::cout << "[Sender] Sending 10 messages..." << std::endl;

  for (int i = 0; i < 10; ++i) {
    Message msg{i, i * 1.5};
    sender.send(msg);
    std::cout << "  Sent: id=" << msg.id << ", value=" << msg.value
              << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  std::cout << "[Sender] Done." << std::endl;
}

void receiver_example() {
  // 等待 Sender 创建共享内存
  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  // 创建 Receiver（连接到已有共享内存）
  shm::Receiver<Message> receiver("/demo");

  std::cout << "[Receiver] Receiving 10 messages..." << std::endl;

  for (int i = 0; i < 10; ++i) {
    auto msg = receiver.receive();
    std::cout << "  Received: id=" << msg.id << ", value=" << msg.value
              << std::endl;
  }

  std::cout << "[Receiver] Done." << std::endl;
}

int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " [sender|receiver]" << std::endl;
    return 1;
  }

  std::string mode = argv[1];
  if (mode == "sender")
    sender_example();
  else if (mode == "receiver")
    receiver_example();
  else
    std::cerr << "Unknown mode: " << mode << std::endl;

  return 0;
}
