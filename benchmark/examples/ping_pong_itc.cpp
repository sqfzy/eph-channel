#include "benchmark/config.hpp"
#include "ping_pong_common.hpp"
#include "shm_channel/duplex_channel.hpp"

#include <iostream>

using namespace benchmark;
using namespace shm::itc;

int main() {
  std::cout << "Starting Thread (ITC) Ping-Pong Benchmark..." << std::endl;

  // 1. 创建线程间通信通道
  auto [sender, receiver] = duplex_channel<MarketData>();

  // 2. 启动消费者线程
  std::thread consumer_thread([recv = std::move(receiver)]() mutable {
    run_consumer(std::move(recv));
  });

  // 3. 在主线程运行生产者
  run_producer(std::move(sender), "shm_itc_latency");

  // 4. 等待消费者线程结束
  consumer_thread.join();

  return 0;
}
