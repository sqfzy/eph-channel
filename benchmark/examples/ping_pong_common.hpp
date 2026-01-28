#pragma once

#include "benchmark/config.hpp"
#include "benchmark/stats.hpp"
#include "benchmark/system.hpp"
#include "benchmark/timer.hpp"

#include <iostream>
#include <string>

using namespace benchmark;

// -----------------------------------------------------------------------------
// Generic Producer
// -----------------------------------------------------------------------------
template <typename Tx, typename Rx>
void run_producer(Tx tx, Rx rx, const std::string &report_name) {
  System::bind_numa(BenchConfig::PRODUCER_NODE, BenchConfig::PRODUCER_CORE);
  System::set_realtime_priority();

  TSCClock clock;
  StatsRecorder stats;
  stats.reserve(BenchConfig::ITERATIONS);

  std::cout << "[Producer] Waiting for consumer..." << std::endl;

  // Handshake / Warmup
  MarketData dummy{};
  tx.send(dummy);    // 发送
  rx.receive(dummy); // 假设 receive 是阻塞的

  std::cout << "[Producer] Warmup (" << BenchConfig::WARMUP_ITERATIONS
            << " iterations)..." << std::endl;
  MarketData msg{};
  for (int i = 0; i < BenchConfig::WARMUP_ITERATIONS; ++i) {
    msg.sequence_id = i + 1;
    tx.send(msg);
    rx.receive(msg);
  }

  std::cout << "[Producer] Running benchmark (" << BenchConfig::ITERATIONS
            << " iterations)..." << std::endl;

  MarketData ack; // 用于接收响应
  for (int i = 0; i < BenchConfig::ITERATIONS; ++i) {
    msg.sequence_id = i + 1;

    uint64_t t0 = TSCClock::now();

    tx.send(msg);
    rx.receive(ack); // 假设 receive 是阻塞的

    uint64_t t1 = TSCClock::now();

    stats.add(i, clock.to_ns(t1 - t0) / 2.0);

    if (ack.sequence_id != msg.sequence_id) {
      std::cerr << "Mismatch! Sent: " << msg.sequence_id
                << " Recv: " << ack.sequence_id << std::endl;
      exit(1);
    }
  }

  // 发送终止信号
  msg.sequence_id = BenchConfig::SEQ_TERMINATE;
  tx.send(msg);
  rx.receive(ack); // 等待确认

  std::cout << "[Producer] Consumer acknowledged termination." << std::endl;

  stats.report(report_name);
}

// -----------------------------------------------------------------------------
// Generic Consumer
// -----------------------------------------------------------------------------
template <typename Rx, typename Tx> void run_consumer(Rx rx, Tx tx) {
  System::bind_numa(BenchConfig::CONSUMER_NODE, BenchConfig::CONSUMER_CORE);
  System::set_realtime_priority();

  std::cout << "[Consumer] Ready." << std::endl;

  MarketData req;
  while (true) {
    // 阻塞接收请求
    rx.receive(req);

    if (req.sequence_id == BenchConfig::SEQ_TERMINATE) {
      tx.send(req); // 回复终止确认
      std::cout << "[Consumer] Termination received. Exiting." << std::endl;
      break;
    }

    // 回显 (Echo)
    tx.send(req);
  }
}
