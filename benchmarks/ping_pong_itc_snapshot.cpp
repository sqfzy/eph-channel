#include "common.hpp"
#include "eph/channel/itc.hpp"
#include <print>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

int main() {
  std::println("Starting Thread (ITC Standard Snapshot) Benchmark...");
  std::println("  - Backend: SeqLock (Single Slot)");
  std::println("  - Metric: Freshness & Read Cost");
  std::println("  - Expectation: High Read Cost due to intense cache contention in same process.");

  // 使用标准 make_snapshot (SeqLock)
  auto [pub, sub] = eph::itc::make_snapshot<MarketData>();

  // 启动消费者线程
  std::thread consumer_thread([sub = std::move(sub)]() mutable {
    run_snapshot_consumer(std::move(sub), "ping_pong_itc_snapshot");
  });

  // 主线程运行生产者 (Flooding)
  run_snapshot_producer(std::move(pub));

  if (consumer_thread.joinable()) {
    consumer_thread.join();
  }

  return 0;
}
