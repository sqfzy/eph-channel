#pragma once

#include "benchmark/config.hpp"
#include "benchmark/stats.hpp"
#include "benchmark/timer.hpp"
#include <eph_channel/platform.hpp>

#include <print>
#include <string>
#include <system_error>

using namespace benchmark;

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

  TSCClock clock;
  StatsRecorder stats;
  stats.reserve(BenchConfig::ITERATIONS);

  std::print("[Producer] Waiting for consumer...\n");

  // Handshake / Warmup
  MarketData dummy{};
  tx.send(dummy);    // 发送
  rx.receive(dummy); // 假设 receive 是阻塞 of

  std::print("[Producer] Warmup ({} iterations)...\n", BenchConfig::WARMUP_ITERATIONS);
  MarketData msg{};
  for (int i = 0; i < BenchConfig::WARMUP_ITERATIONS; ++i) {
    msg.sequence_id = i + 1;
    tx.send(msg);
    rx.receive(msg);
  }

  std::print("[Producer] Running benchmark ({} iterations)...\n", BenchConfig::ITERATIONS);

  MarketData ack; // 用于接收响应
  for (int i = 0; i < BenchConfig::ITERATIONS; ++i) {
    msg.sequence_id = i + 1;

    uint64_t t0 = TSCClock::now();

    tx.send(msg);
    rx.receive(ack); // 假设 receive 是阻塞的

    uint64_t t1 = TSCClock::now();

    stats.add(i, clock.to_ns(t1 - t0) / 2.0);

    if (ack.sequence_id != msg.sequence_id) {
      std::print(stderr, "Mismatch! Sent: {} Recv: {}\n", msg.sequence_id, ack.sequence_id);
      exit(1);
    }
  }

  // 发送终止信号
  msg.sequence_id = BenchConfig::SEQ_TERMINATE;
  tx.send(msg);
  rx.receive(ack); // 等待确认

  std::print("[Producer] Consumer acknowledged termination.\n");

  stats.report(report_name);
}

// -----------------------------------------------------------------------------
// Generic Consumer
// -----------------------------------------------------------------------------
template <typename Rx, typename Tx> 
void run_consumer(Rx rx, Tx tx) {
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
