#include "benchmark/config.hpp"
#include "ping_pong_common.hpp"
#include "eph_channel/channel.hpp"

#include <iostream>
#include <unistd.h>

using namespace benchmark;
using namespace eph::udp;

int main() {
  std::cout << "Starting Process (UDP) Ping-Pong Benchmark..." << std::endl;

  // 定义端口
  // 注意：确保这两个端口在测试机器上未被占用
  uint16_t p2c_port = 12345;
  uint16_t c2p_port = 12346;

  // P2C: Producer -> Consumer
  Receiver<MarketData> p2c_rx(p2c_port);
  Sender<MarketData> p2c_tx("127.0.0.1", p2c_port);

  // C2P: Consumer -> Producer
  Receiver<MarketData> c2p_rx(c2p_port);
  Sender<MarketData> c2p_tx("127.0.0.1", c2p_port);

  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "Fork failed!" << std::endl;
    return 1;
  }

  if (pid == 0) {
    // Child: Consumer
    run_consumer(std::move(p2c_rx), std::move(c2p_tx));
  } else {
    // Parent: Producer
    run_producer(std::move(p2c_tx), std::move(c2p_rx),
                 "bench_ping_pong_udp");
  }

  return 0;
}
