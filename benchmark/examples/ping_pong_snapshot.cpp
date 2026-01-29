#include "benchmark/config.hpp"
#include "eph_channel/channel.hpp"
#include "ping_pong_common.hpp"
#include <limits>
#include <print>

using namespace eph;

// =============================================================================
// Adapters: 将 Snapshot 语义适配为 Queue 语义以复用测试逻辑
// =============================================================================

template <typename T> struct SnapshotTxAdapter {
  snapshot::ipc::Publisher<T> pub;

  explicit SnapshotTxAdapter(snapshot::ipc::Publisher<T> p)
      : pub(std::move(p)) {}

  void send(const T &data) { pub.publish(data); }
};

template <typename T> struct SnapshotRxAdapter {
  snapshot::ipc::Subscriber<T> sub;
  T last_val{};

  explicit SnapshotRxAdapter(snapshot::ipc::Subscriber<T> s)
      : sub(std::move(s)) {
    // 初始化为一个不可能的序列号，确保能检测到 seq=0 的第一个包
    if constexpr (requires { last_val.sequence_id; }) {
      last_val.sequence_id =
          std::numeric_limits<decltype(last_val.sequence_id)>::max();
    }
  }

  // 阻塞直到数据变化 (模拟 Queue receive)
  void receive(T &out) {
    while (true) {
      sub.fetch(out);
      if (out.sequence_id != last_val.sequence_id) {
        last_val = out;
        return;
      }
      cpu_relax();
    }
  }
};

// =============================================================================
// Main Benchmark
// =============================================================================

int main() {
  std::println("Starting Process (Snapshot) Ping-Pong Benchmark...");

  bool use_huge_page = true;
  std::string p2c_name = std::string(BenchConfig::SHM_NAME) + "_snap_p2c";
  std::string c2p_name = std::string(BenchConfig::SHM_NAME) + "_snap_c2p";

  // 创建 Snapshot 通道
  auto [p2c_pub, p2c_sub] = snapshot::ipc::channel<MarketData>(p2c_name);
  auto [c2p_pub, c2p_sub] = snapshot::ipc::channel<MarketData>(c2p_name);

  pid_t pid = fork();
  if (pid < 0) {
    std::println(stderr, "Fork failed!");
    return 1;
  }

  if (pid == 0) {
    // Child (Consumer)
    SnapshotRxAdapter<MarketData> rx(std::move(p2c_sub));
    SnapshotTxAdapter<MarketData> tx(std::move(c2p_pub));

    run_consumer(std::move(rx), std::move(tx));
  } else {
    // Parent (Producer)
    SnapshotTxAdapter<MarketData> tx(std::move(p2c_pub));
    SnapshotRxAdapter<MarketData> rx(std::move(c2p_sub));

    run_producer(std::move(tx), std::move(rx),
                 "bench_ping_pong_snapshot");
  }
}
