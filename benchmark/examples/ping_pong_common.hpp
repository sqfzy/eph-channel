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
template <typename Sender>
void run_producer(Sender sender, const std::string &report_name) {
  // 绑定到指定核心
  System::pin_to_core(BenchConfig::PRODUCER_CORE);
  System::set_realtime_priority();

  TSCClock clock;
  StatsRecorder stats;
  stats.reserve(BenchConfig::ITERATIONS);

  std::cout << "[Producer] Waiting for consumer..." << std::endl;

  // Handshake / Warmup
  MarketData dummy{};
  sender.send_receive(dummy);

  std::cout << "[Producer] Warmup (" << BenchConfig::WARMUP_ITERATIONS
            << " iterations)..." << std::endl;
  MarketData msg{};
  for (int i = 0; i < BenchConfig::WARMUP_ITERATIONS; ++i) {
    sender.send_receive(msg);
  }

  std::cout << "[Producer] Running benchmark (" << BenchConfig::ITERATIONS
            << " iterations)..." << std::endl;

  for (int i = 0; i < BenchConfig::ITERATIONS; ++i) {
    msg.sequence_id = i + 1;

    uint64_t t0 = TSCClock::now();
    // 阻塞式发送并等待回响
    auto ack = sender.send_receive(msg);
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
  sender.send_receive(msg);

  std::cout << "[Producer] Consumer acknowledged termination." << std::endl;

  stats.report(report_name);
}

// -----------------------------------------------------------------------------
// Generic Consumer
// -----------------------------------------------------------------------------
template <typename Receiver>
void run_consumer(Receiver receiver) {
  System::pin_to_core(BenchConfig::CONSUMER_CORE);
  System::set_realtime_priority();

  std::cout << "[Consumer] Ready." << std::endl;

  while (true) {
    bool should_exit = false;

    receiver.receive_send([&](const MarketData &req) -> MarketData {
      if (req.sequence_id == BenchConfig::SEQ_TERMINATE) {
        should_exit = true;
      }
      return req; // Echo
    });

    if (should_exit) {
      std::cout << "[Consumer] Termination received. Exiting." << std::endl;
      break;
    }
  }
}
