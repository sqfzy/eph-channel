#pragma once

#include <eph/benchmark/recorder.hpp>
#include <eph/benchmark/timer.hpp>
#include <eph/platform.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <print>
#include <string>
#include <tabulate/table.hpp>

using namespace eph::benchmark;
using namespace std::chrono_literals;

// -----------------------------------------------------------------------------
// 通用基准测试配置
// -----------------------------------------------------------------------------
struct BenchConfig {
  // 核心绑定
  static constexpr int PRODUCER_NODE = 0;
  static constexpr int CONSUMER_NODE = 0;
  static constexpr int PRODUCER_CORE = 2;
  static constexpr int CONSUMER_CORE = 4;

  // 运行参数
  static constexpr std::chrono::seconds DURATION_SEC = 5s; // 持续运行时间
  static constexpr int WARMUP_COUNT = 100'000;             // 预热次数
  static constexpr uint64_t LOG_INTERVAL = 10'000'000;
  static constexpr uint64_t TIME_CHECK_MASK = 0xFFF; // 4096 次检查一次 TSC

  // 协议常量
  static constexpr uint64_t SEQ_TERMINATE = ~0ULL;
  static constexpr const char *SHM_NAME = "/bench_ping_pong";
  static constexpr size_t QUEUE_CAPACITY = 1024;

  // Iceoryx (保持兼容性)
  static constexpr const char IOX_APP_NAME_PRODUCER[] = "bench-producer";
  static constexpr const char IOX_APP_NAME_CONSUMER[] = "bench-consumer";
  static constexpr const char IOX_SERVICE[] = "BenchService";
  static constexpr const char IOX_INSTANCE[] = "PingPong";
  static constexpr const char IOX_EVENT_PING[] = "Ping";
  static constexpr const char IOX_EVENT_PONG[] = "Pong";
  static constexpr uint64_t IOX_QUEUE_CAPACITY = 1;
  static constexpr uint64_t IOX_HISTORY_CAPACITY = 1;
};

struct alignas(128) MarketData {
  uint64_t timestamp_ns;
  uint64_t sequence_id;
  char payload[80 - 16];
};

// -----------------------------------------------------------------------------
// Queue (Ping-Pong) Producer & Consumer
// -----------------------------------------------------------------------------
template <typename Tx, typename Rx>
void run_queue_producer(Tx tx, Rx rx, const std::string &report_name) {
  try {
    eph::bind_numa(BenchConfig::PRODUCER_NODE, BenchConfig::PRODUCER_CORE);
    eph::set_realtime_priority();
  } catch (const std::exception &e) {
    std::print(stderr, "[Producer] Setup warning: {}\n", e.what());
  }

  TSC::init();
  Recorder stats(report_name);

  std::print("[Producer] Waiting for consumer...\n");

  // Handshake
  MarketData dummy{};
  tx.send(dummy);
  rx.receive(dummy);

  // Warmup
  std::print("[Producer] Warming up ({} iters)...\n",
             BenchConfig::WARMUP_COUNT);
  for (int i = 0; i < BenchConfig::WARMUP_COUNT; ++i) {
    dummy.sequence_id = i;
    tx.send(dummy);
    rx.receive(dummy);
  }

  // 计算目标 Cycles
  uint64_t duration_cycles = TSC::to_cycles(BenchConfig::DURATION_SEC);
  std::print(
      "[Producer] Started. Running Ping-Pong for {} seconds ({} cycles)...\n",
      BenchConfig::DURATION_SEC, duration_cycles);

  MarketData msg{};
  MarketData ack{};
  msg.sequence_id = 0;

  uint64_t start_tsc = TSC::now();
  uint64_t stop_tsc = start_tsc + duration_cycles;
  uint64_t count = 0;

  while (true) {
    // 仅使用 TSC 指令检查超时
    if ((count & BenchConfig::TIME_CHECK_MASK) == 0) {
      if (TSC::now() > stop_tsc)
        break;
    }

    msg.sequence_id++;

    uint64_t t0 = TSC::now();
    tx.send(msg);
    rx.receive(ack);
    uint64_t t1 = TSC::now();

    stats.record(static_cast<double>(t1 - t0) / 2.0);

    if (ack.sequence_id != msg.sequence_id) {
      std::print(stderr, "Mismatch! Sent: {} Recv: {}\n", msg.sequence_id,
                 ack.sequence_id);
      std::terminate();
    }

    count++;
    if (count % BenchConfig::LOG_INTERVAL == 0) {
      std::print("[Producer] Processed {} round-trips...\n", count);
    }
  }

  msg.sequence_id = BenchConfig::SEQ_TERMINATE;
  tx.send(msg);
  rx.receive(ack);

  std::print("[Producer] Finished. Total round-trips: {}\n", count);
  stats.print_report();
  stats.export_samples_to_csv();
  stats.export_json();
}

template <typename Rx, typename Tx> void run_queue_consumer(Rx rx, Tx tx) {
  try {
    eph::bind_numa(BenchConfig::CONSUMER_NODE, BenchConfig::CONSUMER_CORE);
    eph::set_realtime_priority();
  } catch (const std::exception &e) {
    std::print(stderr, "[Consumer] Setup warning: {}\n", e.what());
  }

  std::print("[Consumer] Ready.\n");

  MarketData req;
  while (true) {
    rx.receive(req);
    if (req.sequence_id == BenchConfig::SEQ_TERMINATE) {
      tx.send(req);
      std::print("[Consumer] Termination received. Exiting.\n");
      break;
    }
    tx.send(req);
  }
}

// -----------------------------------------------------------------------------
// Snapshot Producer: 全速写入
// -----------------------------------------------------------------------------
template <typename Pub> void run_snapshot_producer(Pub pub) {
  try {
    eph::bind_numa(BenchConfig::PRODUCER_NODE, BenchConfig::PRODUCER_CORE);
    eph::set_realtime_priority();
  } catch (const std::exception &e) {
    std::print(stderr, "[Producer] Setup warning: {}\n", e.what());
  }

  TSC::init();

  std::print("[Producer] Warming up ({} updates)...\n",
             BenchConfig::WARMUP_COUNT);
  MarketData msg{};
  msg.sequence_id = 0;
  for (int i = 0; i < BenchConfig::WARMUP_COUNT; ++i) {
    msg.sequence_id++;
    msg.timestamp_ns = TSC::now();
    pub.publish(msg);
    if (i % 1000 == 0)
      eph::cpu_relax();
  }

  uint64_t duration_cycles = TSC::to_cycles(BenchConfig::DURATION_SEC);
  std::print("[Producer] Started. Flooding updates for {} seconds...\n",
             BenchConfig::DURATION_SEC);

  uint64_t start_tsc = TSC::now();
  uint64_t stop_tsc = start_tsc + duration_cycles;
  uint64_t count = 0;

  while (true) {
    if ((count & BenchConfig::TIME_CHECK_MASK) == 0) {
      if (TSC::now() > stop_tsc)
        break;
    }

    msg.sequence_id++;
    msg.timestamp_ns = TSC::now();
    pub.publish(msg);

    count++;
    if (count % BenchConfig::LOG_INTERVAL == 0) {
      std::print("[Producer] Published {} msgs...\n", count);
    }
  }

  msg.sequence_id = BenchConfig::SEQ_TERMINATE;
  pub.publish(msg);

  std::print("[Producer] Finished. Total published: {}\n", count);
}

// -----------------------------------------------------------------------------
// Snapshot Consumer: 全速读取
// -----------------------------------------------------------------------------
template <typename Sub>
void run_snapshot_consumer(Sub sub, const std::string &title_prefix) {
  try {
    eph::bind_numa(BenchConfig::CONSUMER_NODE, BenchConfig::CONSUMER_CORE);
    eph::set_realtime_priority();
  } catch (const std::exception &e) {
    std::print(stderr, "[Consumer] Setup warning: {}\n", e.what());
  }

  Recorder freshness_stats(title_prefix + "_freshness");
  Recorder read_cost_stats(title_prefix + "_read_cost");

  std::print("[Consumer] Ready. Polling for updates...\n");

  MarketData data{};
  uint64_t last_seq = 0;
  uint64_t received_count = 0;
  uint64_t skipped_count = 0;

  while (true) {
    uint64_t t_start_read = TSC::now();
    data = sub.fetch();
    uint64_t t_end_read = TSC::now();

    read_cost_stats.record(static_cast<double>(t_end_read - t_start_read));

    if (data.sequence_id == BenchConfig::SEQ_TERMINATE)
      break;

    if (data.sequence_id <= last_seq) {
      eph::cpu_relax();
      continue;
    }

    if (last_seq > 0)
      skipped_count += (data.sequence_id - last_seq - 1);
    last_seq = data.sequence_id;
    received_count++;

    double age_cycles = static_cast<double>(t_end_read - data.timestamp_ns);
    if (age_cycles > 0)
      freshness_stats.record(age_cycles);
  }

  std::print("\n[Consumer] Benchmark Finished ({})\n", title_prefix);
  std::print("Total Updates Received: {}\n", received_count);
  std::print("Total Updates Skipped : {} (Conflation Rate: {:.2f}%)\n",
             skipped_count,
             100.0 * skipped_count / (received_count + skipped_count));

  freshness_stats.print_report();
  read_cost_stats.print_report();

  freshness_stats.export_samples_to_csv("outputs");
  read_cost_stats.export_samples_to_csv("outputs");
}

struct MatrixRecord {
  size_t data_size;
  size_t buf_size;
  Stats stats;
};

// 通用正交测试运行器
// Kernel 参数：一个接受 <size_t D, size_t B> 模板参数的 Lambda
template <size_t... Ds, size_t... Bs, typename Kernel>
void run_benchmark_matrix(std::string_view title,
                          std::integer_sequence<size_t, Ds...>,
                          std::integer_sequence<size_t, Bs...>,
                          Kernel &&kernel) {
  using namespace tabulate;
  Table table;

  table.add_row(Table::Row_t{"D \\ B", std::to_string(Bs)...});

  (
      [&] {
        Table::Row_t row;
        row.push_back(std::to_string(Ds));

        (
            [&] {
              auto s = kernel.template operator()<Ds, Bs>();
              row.push_back(std::format("{:.2f}", s.avg_ns));
            }(),
            ...);

        table.add_row(row);
      }(),
      ...);

  table[0]
      .format()
      .font_align(FontAlign::center)
      .font_style({FontStyle::bold})
      .font_color(Color::yellow);

  table.column(0)
      .format()
      .font_color(Color::cyan)
      .font_style({FontStyle::bold});

  std::println("\n>>> {} Matrix (ns)", title);
  std::cout << table << std::endl;
}
