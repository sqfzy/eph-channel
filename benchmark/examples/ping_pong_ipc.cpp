#include "benchmark/config.hpp"
#include "ping_pong_common.hpp"
#include "shm_channel/duplex_channel.hpp"

#include <iostream>

using namespace benchmark;
using namespace shm::ipc;

int main() {
  std::cout << "Starting Process (IPC) Ping-Pong Benchmark..." << std::endl;

  auto [sender, receiver] = duplex_channel<MarketData>(BenchConfig::SHM_NAME);

  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "Fork failed!" << std::endl;
    return 1;
  }

  if (pid == 0) {
    // 子进程运行消费者
    run_consumer(std::move(receiver));
  } else {
    // 父进程运行生产者
    run_producer(std::move(sender), "shm_ipc_latency");
  }

  return 0;
}
