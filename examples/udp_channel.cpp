#include "eph/channel.hpp"
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace eph::udp;

// 定义传输的数据结构
struct Message {
  int id;
  double value;
};

// 端口配置
static constexpr uint16_t PORT = 12345;
static const std::string IP = "127.0.0.1";

void run_sender() {
  // UDP 特性：发送者需要等待接收者 Bind
  // 端口，否则数据包会丢失（特别是本地回环极快的情况下）
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  // 创建 Sender (连接到目标 IP:Port)
  Sender<Message> sender(IP, PORT);
  std::cout << "[Sender]   Connected to " << IP << ":" << PORT
            << ". Sending 10 messages..." << std::endl;

  for (int i = 0; i < 10; ++i) {
    Message msg{i, i * 1.5};

    // 发送数据
    sender.send(msg);

    std::cout << "[Sender]   Sent: id=" << msg.id << ", value=" << msg.value
              << std::endl;

    // 模拟生产间隔
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  std::cout << "[Sender]   Done. Exiting." << std::endl;
}

void run_receiver() {
  // 创建 Receiver (绑定本地端口)
  // 注意：Receiver 应当尽早启动
  try {
    Receiver<Message> receiver(PORT);
    std::cout << "[Receiver] Bound to port " << PORT
              << ". Waiting for messages..." << std::endl;

    for (int i = 0; i < 10; ++i) {
      // 阻塞接收
      auto msg = receiver.receive();

      std::cout << "[Receiver] Received: id=" << msg.id
                << ", value=" << msg.value << std::endl;
    }
    std::cout << "[Receiver] Done. Exiting." << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[Receiver] Error: " << e.what() << std::endl;
  }
}

int main() {
  std::cout << "=== Simple UDP Channel Demo ===" << std::endl;

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

    // 等待子进程结束
    wait(NULL);
    std::cout << "=== Demo Finished ===" << std::endl;
  }

  return 0;
}
