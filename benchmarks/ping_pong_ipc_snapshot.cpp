#include "common.hpp"
#include "eph/channel/ipc.hpp"
#include <print>
#include <unistd.h>

using namespace eph;
using namespace eph::benchmark;

int main() {
  std::println("Starting Process (IPC Standard Snapshot) Benchmark...");
  std::println("  - Backend: SeqLock (Single Slot)");
  std::println("  - Metric: Freshness & Read Cost");
  std::println("  - Expectation: High Read Cost under contention due to spin-retry.");

  bool use_huge_page = true;
  std::string shm_name = std::string(BenchConfig::SHM_NAME) + "_std_snapshot";

  // 使用标准 make_snapshot (SeqLock)
  auto [pub, sub] = eph::ipc::make_snapshot<MarketData>(shm_name, use_huge_page);

  pid_t pid = fork();
  if (pid < 0) {
    std::println(stderr, "Fork failed!");
    return 1;
  }

  if (pid == 0) {
    // Child: Consumer (Polling)
    run_snapshot_consumer(std::move(sub), "ping_pong_ipc_snapshot");
  } else {
    // Parent: Producer (Flooding)
    run_snapshot_producer(std::move(pub));
  }

  return 0;
}
