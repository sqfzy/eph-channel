#pragma once

#include <eph/platform.hpp>
#include <eph/benchmark/recorder.hpp>

#include <print>
#include <string>
#include <system_error>

#pragma once

#include <cstddef>
#include <cstdint>

using namespace eph::benchmark;

// -----------------------------------------------------------------------------
// 通用基准测试配置
// -----------------------------------------------------------------------------
struct BenchConfig {
  // 测试参数
  static constexpr int ITERATIONS = 1000000;      // 测试迭代次数
  static constexpr int WARMUP_ITERATIONS = 10000; // 预热次数

  // NUMA 节点绑定
  static constexpr int PRODUCER_NODE = 0;
  static constexpr int CONSUMER_NODE = 0;

  // CPU 绑定
  static constexpr int PRODUCER_CORE = 2;
  static constexpr int CONSUMER_CORE = 4;

  // 终止信号
  static constexpr uint64_t SEQ_TERMINATE = ~0ULL;

  // 共享内存配置
  static constexpr const char *SHM_NAME = "/bench_ping_pong";
  static constexpr size_t QUEUE_CAPACITY = 1024;

  // Iceoryx 配置
  static constexpr const char IOX_APP_NAME_PRODUCER[] = "bench-producer";
  static constexpr const char IOX_APP_NAME_CONSUMER[] = "bench-consumer";
  static constexpr const char IOX_SERVICE[] = "BenchService";
  static constexpr const char IOX_INSTANCE[] = "PingPong";
  static constexpr const char IOX_EVENT_PING[] = "Ping";
  static constexpr const char IOX_EVENT_PONG[] = "Pong";

  // 队列配置
  static constexpr uint64_t IOX_QUEUE_CAPACITY = 1;
  static constexpr uint64_t IOX_HISTORY_CAPACITY = 1;
};

// -----------------------------------------------------------------------------
// 测试数据结构
// -----------------------------------------------------------------------------
struct alignas(128) MarketData {
  uint64_t timestamp_ns;
  uint64_t sequence_id;
  char payload[80 - 16];
};

// -----------------------------------------------------------------------------
// Generic Producer
// -----------------------------------------------------------------------------
template <typename Tx, typename Rx>
void run_producer(Tx tx, Rx rx, const std::string &report_name) {
  try {
    eph::bind_numa(BenchConfig::PRODUCER_NODE, BenchConfig::PRODUCER_CORE);
  } catch (const std::system_error &e) {
    std::print(stderr, "[Producer] NUMA binding failed: {}\n", e.what());
  }

  try {
    eph::set_realtime_priority();
  } catch (const std::system_error &e) {
    std::print(stderr, "[Producer] RT priority failed: {}\n", e.what());
  }

  // 初始化 TSC 计时器
  TSC::global().init();

  Recorder stats(report_name);

  std::print("[Producer] Waiting for consumer...\n");

  // Handshake / Warmup
  MarketData dummy{};
  tx.send(dummy);
  rx.receive(dummy);

  std::print("[Producer] Warmup ({} iterations)...\n",
             BenchConfig::WARMUP_ITERATIONS);
  MarketData msg{};
  for (int i = 0; i < BenchConfig::WARMUP_ITERATIONS; ++i) {
    msg.sequence_id = i + 1;
    tx.send(msg);
    rx.receive(msg);
  }

  std::print("[Producer] Running benchmark ({} iterations)...\n",
             BenchConfig::ITERATIONS);

  MarketData ack;
  for (int i = 0; i < BenchConfig::ITERATIONS; ++i) {
    msg.sequence_id = i + 1;

    uint64_t t0 = TSC::global().now();

    tx.send(msg);
    rx.receive(ack);

    uint64_t t1 = TSC::global().now();

    // 计算 RTT/2 的 cycles
    double latency_cycles = static_cast<double>(t1 - t0) / 2.0;

    stats.record(latency_cycles);

    if (ack.sequence_id != msg.sequence_id) {
      std::print(stderr, "Mismatch! Sent: {} Recv: {}\n", msg.sequence_id,
                 ack.sequence_id);
      exit(1);
    }
  }

  // 发送终止信号
  msg.sequence_id = BenchConfig::SEQ_TERMINATE;
  tx.send(msg);
  rx.receive(ack);

  std::print("[Producer] Consumer acknowledged termination.\n");

  // 输出报告
  stats.print_report();
  stats.export_samples_to_csv();
  stats.export_json();
}

// -----------------------------------------------------------------------------
// Generic Consumer
// -----------------------------------------------------------------------------
template <typename Rx, typename Tx> void run_consumer(Rx rx, Tx tx) {
  try {
    eph::bind_numa(BenchConfig::CONSUMER_NODE, BenchConfig::CONSUMER_CORE);
  } catch (const std::system_error &e) {
    std::print(stderr, "[Consumer] NUMA binding failed: {}\n", e.what());
  }

  try {
    eph::set_realtime_priority();
  } catch (const std::system_error &e) {
    std::print(stderr, "[Consumer] RT priority failed: {}\n", e.what());
  }

  std::print("[Consumer] Ready.\n");

  MarketData req;
  while (true) {
    // 阻塞接收请求
    rx.receive(req);

    if (req.sequence_id == BenchConfig::SEQ_TERMINATE) {
      tx.send(req); // 回复终止确认
      std::print("[Consumer] Termination received. Exiting.\n");
      break;
    }

    // 回显 (Echo)
    tx.send(req);
  }
}
