#include "eph/channel.hpp"
#include <iostream>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>

using namespace eph;

struct Message {
  int id;
  double value;
};

void run_publisher(const std::string& name) {
  // 创建 Publisher (Owner)
  auto [pub, _] = snapshot::ipc::channel<Message>(name);
  std::cout << "[Publisher]  Shared Memory Created." << std::endl;

  for (int i = 0; i < 10; ++i) {
    // 模拟更新状态
    pub.publish([i](Message& c) {
        c.id = i;
        c.value = 20.0f + static_cast<float>(i) * 0.5f;
    });
    
    std::cout << "[Publisher]  Updated: id=" << i << std::endl;
    // 稍作休眠
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }
  
  // 发送结束标志
  pub.publish({-1, 0.0f});
  std::cout << "[Publisher]  Done. Exiting." << std::endl;
}

void run_subscriber(const std::string& name) {
  // 等待 SHM 创建
  std::this_thread::sleep_for(std::chrono::milliseconds(100));

  try {
    // 连接 Publisher (User)
    snapshot::ipc::Subscriber<Message> sub(
        snapshot::ipc::IpcBackend<Message>(name, false)
    );
    std::cout << "[Subscriber] Connected. Monitoring updates..." << std::endl;

    int last_id = -999;
    while (true) {
      Message cfg;
      sub.fetch(cfg); // 获取当前最新状态

      if (cfg.id == -1) break;

      // 只有数据变了才打印
      if (cfg.id != last_id) {
          std::cout << "[Subscriber] Observed: id=" << cfg.id 
                    << ", temp=" << cfg.value << std::endl;
          last_id = cfg.id;
      } else {
          std::this_thread::sleep_for(std::chrono::milliseconds(10));
      }
    }
    std::cout << "[Subscriber] Done. Exiting." << std::endl;
  } catch (const std::exception &e) {
    std::cerr << "[Subscriber] Error: " << e.what() << std::endl;
  }
}

int main() {
  std::string shm_name = "/demo_simple_snapshot";
  std::cout << "=== Simple Snapshot Channel Demo ===" << std::endl;

  pid_t pid = fork();

  if (pid < 0) {
    return 1;
  }

  if (pid == 0) {
    run_subscriber(shm_name);
  } else {
    run_publisher(shm_name);
    wait(NULL);
    std::cout << "=== Demo Finished ===" << std::endl;
  }

  return 0;
}
