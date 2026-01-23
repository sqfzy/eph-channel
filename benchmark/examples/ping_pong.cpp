#include "benchmark/config.hpp"
#include "benchmark/stats.hpp"
#include "benchmark/system.hpp"
#include "benchmark/timer.hpp"
#include "shm_channel/duplex_channel.hpp"

#include <iostream>
#include <string_view>
#include <thread>

using namespace benchmark;

// -----------------------------------------------------------------------------
// Producer
// -----------------------------------------------------------------------------
void run_producer() {
  System::pin_to_core(BenchConfig::PRODUCER_CORE);
  System::set_realtime_priority();

  shm::DuplexSender<MarketData> sender(BenchConfig::SHM_NAME);

  TSCClock clock;
  StatsRecorder stats;
  stats.reserve(BenchConfig::ITERATIONS);

  std::cout << "[Producer] Waiting for consumer..." << std::endl;
  sender.handshake();

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
    auto ack = sender.send_receive(msg);
    uint64_t t1 = TSCClock::now();

    if (ack.sequence_id != msg.sequence_id) {
      std::cerr << "Mismatch! Expected " << msg.sequence_id << ", got "
                << ack.sequence_id << std::endl;
      exit(1);
    }

    stats.add(i, clock.to_ns(t1 - t0) / 2.0);
  }

  std::cout << "[Producer] Sending termination signal..." << std::endl;
  msg.sequence_id = BenchConfig::SEQ_TERMINATE;
  auto ack = sender.send_receive(msg);

  if (ack.sequence_id == BenchConfig::SEQ_TERMINATE) {
    std::cout << "[Producer] Consumer acknowledged termination." << std::endl;
  }

  stats.report("shm_latency");
}

// -----------------------------------------------------------------------------
// Consumer
// -----------------------------------------------------------------------------
void run_consumer() {
  System::pin_to_core(BenchConfig::CONSUMER_CORE);
  System::set_realtime_priority();

  std::this_thread::sleep_for(std::chrono::milliseconds(500));

  shm::DuplexReceiver<MarketData> receiver(BenchConfig::SHM_NAME);

  std::cout << "[Consumer] Ready." << std::endl;
  receiver.handshake();

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

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char **argv) {
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " [producer|consumer]" << std::endl;
    return 1;
  }

  std::string_view mode = argv[1];
  if (mode == "producer")
    run_producer();
  else if (mode == "consumer")
    run_consumer();
  else
    std::cerr << "Unknown mode: " << mode << std::endl;

  return 0;
}
