#include "common.hpp"
#include "eph/channel.hpp"

#include <iostream>
#include <thread>

using namespace eph::benchmark;
using namespace eph::itc;

int main() {
  std::cout << "Starting Thread (ITC) Ping-Pong Benchmark..." << std::endl;

  // NOTE: 若为 true。先执行
  // sudo sysctl -w vm.nr_hugepages=20
  // cat /proc/sys/vm/nr_hugepages
  bool use_huge_page = true;

  // 1. 创建两个单向通道
  // p2c: Producer -> Consumer
  auto [p2c_tx, p2c_rx] = channel<MarketData>(use_huge_page);
  // c2p: Consumer -> Producer
  auto [c2p_tx, c2p_rx] = channel<MarketData>(use_huge_page);

  // 2. 启动消费者线程
  // 消费者持有 p2c 的接收端 和 c2p 的发送端
  std::thread consumer_thread([rx = std::move(p2c_rx), tx = std::move(c2p_tx)]() mutable {
    run_consumer(std::move(rx), std::move(tx));
  });

  // 3. 在主线程运行生产者
  // 生产者持有 p2c 的发送端 和 c2p 的接收端
  run_producer(std::move(p2c_tx), std::move(c2p_rx), "bench_ping_pong_itc");

  // 4. 等待消费者线程结束
  consumer_thread.join();

  return 0;
}
