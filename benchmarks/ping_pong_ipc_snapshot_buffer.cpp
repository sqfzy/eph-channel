#include "common.hpp"
#include "eph/channel/ipc.hpp"
#include <print>
#include <unistd.h>

using namespace eph;
using namespace eph::benchmark;

int main() {
  std::println("Starting Process (IPC Buffered Snapshot) Benchmark...");
  std::println("  - Backend: SeqLockBuffer (Multi-Slot)");
  std::println("  - Metric: Freshness & Read Cost");
  std::println(
      "  - Expectation: Very Low Read Cost due to cache line isolation.");

  bool use_huge_page = true;
  std::string shm_name = std::string(BenchConfig::SHM_NAME) + "_buf_snapshot";

  // 使用 make_buffered_snapshot (SeqLockBuffer)
  // 默认 8 个槽位
  auto [pub, sub] =
      eph::ipc::make_buffered_snapshot<MarketData>(shm_name, use_huge_page);

  pid_t pid = fork();
  if (pid < 0) {
    std::println(stderr, "Fork failed!");
    return 1;
  }

  if (pid == 0) {
    // Child: Consumer (Polling)
    run_snapshot_consumer(std::move(sub), "ping_pong_ipc_buf_snapshot");
  } else {
    // Parent: Producer (Flooding)
    run_snapshot_producer(std::move(pub));
  }

  return 0;
}
