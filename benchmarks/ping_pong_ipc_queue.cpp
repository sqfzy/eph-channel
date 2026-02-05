#include "common.hpp"
#include "eph/channel/ipc.hpp"
#include <print>
#include <unistd.h>

using namespace eph;
using namespace eph::benchmark;

int main() {
  std::println("Starting Process (IPC Queue) Ping-Pong Benchmark...");
  std::println("  - Backend: RingBuffer (Wait-free SPSC)");
  std::println("  - Metric: End-to-End Latency (RTT/2)");

  bool use_huge_page = true;
  std::string p2c_name = std::string(BenchConfig::SHM_NAME) + "_queue_p2c";
  std::string c2p_name = std::string(BenchConfig::SHM_NAME) + "_queue_c2p";

  // 创建两个单向队列构成回路
  // P2C: Producer -> Consumer
  auto [p2c_tx, p2c_rx] = eph::ipc::make_queue<MarketData, BenchConfig::QUEUE_CAPACITY>(p2c_name, use_huge_page);
  // C2P: Consumer -> Producer
  auto [c2p_tx, c2p_rx] = eph::ipc::make_queue<MarketData, BenchConfig::QUEUE_CAPACITY>(c2p_name, use_huge_page);

  pid_t pid = fork();
  if (pid < 0) {
    std::println(stderr, "Fork failed!");
    return 1;
  }

  if (pid == 0) {
    // Child: Consumer
    // 接收 P2C，发送 C2P
    run_queue_consumer(std::move(p2c_rx), std::move(c2p_tx));
  } else {
    // Parent: Producer
    // 发送 P2C，接收 C2P
    run_queue_producer(std::move(p2c_tx), std::move(c2p_rx), "ping_pong_ipc_queue");
  }

  return 0;
}
