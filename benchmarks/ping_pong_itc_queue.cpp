#include "common.hpp"
#include "eph/channel/itc.hpp"
#include <print>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

int main() {
  std::println("Starting Thread (ITC Queue) Ping-Pong Benchmark...");
  std::println("  - Backend: RingBuffer (std::shared_ptr)");
  std::println("  - Metric: End-to-End Latency (RTT/2)");

  // 创建两个单向队列构成回路
  // P2C: Producer -> Consumer
  auto [p2c_tx, p2c_rx] = eph::itc::make_queue<MarketData, BenchConfig::QUEUE_CAPACITY>();
  // C2P: Consumer -> Producer
  auto [c2p_tx, c2p_rx] = eph::itc::make_queue<MarketData, BenchConfig::QUEUE_CAPACITY>();

  // 启动消费者线程
  std::thread consumer_thread([rx = std::move(p2c_rx), tx = std::move(c2p_tx)]() mutable {
    run_queue_consumer(std::move(rx), std::move(tx));
  });

  // 主线程运行生产者
  run_queue_producer(std::move(p2c_tx), std::move(c2p_rx), "ping_pong_itc_queue");

  // 等待测试结束
  if (consumer_thread.joinable()) {
    consumer_thread.join();
  }

  return 0;
}
