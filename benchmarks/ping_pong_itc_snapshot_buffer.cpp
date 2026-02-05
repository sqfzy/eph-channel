#include "common.hpp"
#include "eph/channel/itc.hpp"
#include <print>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

int main() {
  std::println("Starting Thread (ITC Buffered Snapshot) Benchmark...");
  std::println("  - Backend: SeqLockBuffer (Multi-Slot)");
  std::println("  - Metric: Freshness & Read Cost");
  std::println("  - Expectation: Lowest Read Cost via Cache Line Isolation.");

  // 使用 make_buffered_snapshot (SeqLockBuffer)
  // 默认 8 个槽位
  auto [pub, sub] = eph::itc::make_buffered_snapshot<MarketData>();

  // 启动消费者线程
  std::thread consumer_thread([sub = std::move(sub)]() mutable {
    run_snapshot_consumer(std::move(sub), "ping_pong_itc_buf_snapshot");
  });

  // 主线程运行生产者
  run_snapshot_producer(std::move(pub));

  if (consumer_thread.joinable()) {
    consumer_thread.join();
  }

  return 0;
}
