#include "shm_channel/channel.hpp"
#include <iostream>
#include <thread>
#include <unistd.h>
#include <sys/wait.h>

using namespace shm::ipc;

struct Message {
  int id;
  double value;
};

void run_sender() {
  // 创建 Sender（自动创建共享内存，Owner）
  Sender<Message> sender("/demo_simple");
  std::cout << "[Sender]   Shared Memory Created. Sending 10 messages..." << std::endl;

  for (int i = 0; i < 10; ++i) {
    Message msg{i, i * 1.5};
    sender.send(msg);
    std::cout << "[Sender]   Sent: id=" << msg.id << ", value=" << msg.value << std::endl;
    // 稍微慢一点，方便观察
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "[Sender]   Done. Exiting." << std::endl;
}

void run_receiver() {
  // 稍微等待一下父进程创建共享内存文件
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // 创建 Receiver（连接到已有共享内存，User）
  try {
    Receiver<Message> receiver("/demo_simple");
    std::cout << "[Receiver] Connected. Waiting for messages..." << std::endl;

    for (int i = 0; i < 10; ++i) {
      auto msg = receiver.receive();
      std::cout << "[Receiver] Received: id=" << msg.id << ", value=" << msg.value << std::endl;
    }
    std::cout << "[Receiver] Done. Exiting." << std::endl;
  } catch (const std::exception& e) {
    std::cerr << "[Receiver] Error: " << e.what() << std::endl;
  }
}

int main() {
  std::cout << "=== Simple SPSC Channel Demo (Auto-Fork) ===" << std::endl;
  
  // 使用 fork 创建子进程
  pid_t pid = fork();

  if (pid < 0) {
    std::cerr << "Fork failed!" << std::endl;
    return 1;
  }

  if (pid == 0) {
    // --- 子进程充当消费者 (Receiver) ---
    run_receiver();
  } else {
    // --- 父进程充当生产者 (Sender) ---
    run_sender();

    // 等待子进程结束，防止僵尸进程
    wait(NULL);
    std::cout << "=== Demo Finished ===" << std::endl;
  }

  return 0;
}
