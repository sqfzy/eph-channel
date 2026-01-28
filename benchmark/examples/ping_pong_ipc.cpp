#include "benchmark/config.hpp"
#include "eph_channel/channel.hpp"
#include "ping_pong_common.hpp"

#include <cassert>
#include <iostream>
#include <unistd.h>

using namespace benchmark;
using namespace eph::ipc;

int main() {
  std::cout << "Starting Process (IPC) Ping-Pong Benchmark..." << std::endl;

  // NOTE: 若为 true。先执行
  // sudo sysctl -w vm.nr_hugepages=20
  // cat /proc/sys/vm/nr_hugepages
  bool use_huge_page = true;

  // 定义两个不同的 SHM 名称
  std::string p2c_name = std::string(BenchConfig::SHM_NAME) + "_p2c";
  std::string c2p_name = std::string(BenchConfig::SHM_NAME) + "_c2p";

  // 1. 创建通道
  auto [p2c_tx, p2c_rx] = channel<MarketData>(p2c_name, use_huge_page);
  auto [c2p_tx, c2p_rx] = channel<MarketData>(c2p_name, use_huge_page);

  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "Fork failed!" << std::endl;
    return 1;
  }

  if (pid == 0) {
    run_consumer(std::move(p2c_rx), std::move(c2p_tx));
  } else {
    run_producer(std::move(p2c_tx), std::move(c2p_rx),
                 "bench_ping_pong_ipc_latency");
  }

  return 0;
}
