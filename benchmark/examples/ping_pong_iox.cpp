#include "benchmark/config.hpp"
#include "benchmark/timer.hpp"
#include "benchmark/recorder.hpp" 

#include <eph_channel/platform.hpp>
#include <iceoryx_posh/popo/publisher.hpp>
#include <iceoryx_posh/popo/subscriber.hpp>
#include <iceoryx_posh/runtime/posh_runtime.hpp>

#include <print>
#include <thread>

using namespace benchmark;

// -----------------------------------------------------------------------------
// 辅助函数：等待数据可用
// -----------------------------------------------------------------------------
template <typename T> void wait_for_data(iox::popo::Subscriber<T> &sub) {
  while (!sub.hasData()) {
    // 自旋等待
    eph::cpu_relax();
  }
}

// -----------------------------------------------------------------------------
// Producer（发送 Ping，接收 Pong）
// -----------------------------------------------------------------------------
void run_producer() {
  eph::bind_numa(BenchConfig::PRODUCER_NODE, BenchConfig::PRODUCER_CORE);
  eph::set_realtime_priority();
  
  // 初始化 TSC
  TSC::get().init();

  iox::runtime::PoshRuntime::initRuntime(BenchConfig::IOX_APP_NAME_PRODUCER);

  iox::popo::Publisher<MarketData> ping_pub(
      {BenchConfig::IOX_SERVICE, BenchConfig::IOX_INSTANCE,
       BenchConfig::IOX_EVENT_PING},
      iox::popo::PublisherOptions{.historyCapacity =
                                      BenchConfig::IOX_HISTORY_CAPACITY});

  iox::popo::Subscriber<MarketData> pong_sub(
      {BenchConfig::IOX_SERVICE, BenchConfig::IOX_INSTANCE,
       BenchConfig::IOX_EVENT_PONG},
      iox::popo::SubscriberOptions{.queueCapacity =
                                       BenchConfig::IOX_QUEUE_CAPACITY,
                                   .historyRequest = 0});

  Recorder stats("bench_ping_pong_iox");

  std::print("[Producer] Waiting for consumer...\n");
  while (!ping_pub.hasSubscribers()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  std::print("[Producer] Warmup ({} iterations)...\n", BenchConfig::WARMUP_ITERATIONS);
    for (int i = 0; i < BenchConfig::WARMUP_ITERATIONS; ++i) {
    ping_pub.loan()
        .and_then([&](auto &sample) {
          sample->sequence_id = 0;
          sample->timestamp_ns = 0;
          sample.publish();
        })
        .or_else([](auto &error) {
           std::print(stderr, "Warmup loan failed: {}\n", (int)error);
        });

    wait_for_data(pong_sub);
    pong_sub.take().and_then([](const auto &) {});
  }

  std::print("[Producer] Running benchmark ({} iterations)...\n", BenchConfig::ITERATIONS);

  for (int i = 0; i < BenchConfig::ITERATIONS; ++i) {
    uint64_t t0 = TSC::get().now();

    ping_pub.loan()
        .and_then([&](auto &sample) {
          sample->sequence_id = i + 1;
          sample->timestamp_ns = t0;
          sample.publish();
        })
        .or_else([](auto &error) {
          std::print(stderr, "Loan failed: {}\n", (int)error);
          exit(1);
        });

    wait_for_data(pong_sub);

    pong_sub.take()
        .and_then([&](const auto &sample) {
          uint64_t t1 = TSC::get().now();

          if (sample->sequence_id != static_cast<uint64_t>(i + 1)) {
            std::print(stderr, "Sequence mismatch! Expected {}, got {}\n", i + 1, sample->sequence_id);
            exit(1);
          }

          double latency_cycles = static_cast<double>(t1 - t0) / 2.0;
          stats.record(latency_cycles);
        })
        .or_else([](auto &error) {
          std::print(stderr, "Take failed: {}\n", (int)error);
          exit(1);
        });
  }

  std::print("[Producer] Sending termination signal...\n");
  ping_pub.loan().and_then([&](auto &sample) {
    sample->sequence_id = BenchConfig::SEQ_TERMINATE;
    sample.publish();
  });

  wait_for_data(pong_sub);
  pong_sub.take().and_then([](const auto &sample) {
    if (sample->sequence_id == BenchConfig::SEQ_TERMINATE) {
      std::print("[Producer] Consumer acknowledged termination.\n");
    }
  });

  stats.print_report();
  stats.export_samples_to_csv();
  stats.export_json();
}

// -----------------------------------------------------------------------------
// Consumer（接收 Ping，发送 Pong）
// -----------------------------------------------------------------------------
void run_consumer() {
  eph::bind_numa(BenchConfig::CONSUMER_NODE, BenchConfig::CONSUMER_CORE);
  eph::set_realtime_priority();

  // 初始化 Iceoryx 运行时
  iox::runtime::PoshRuntime::initRuntime(BenchConfig::IOX_APP_NAME_CONSUMER);

  // 创建 Subscriber（接收 Ping）
  iox::popo::Subscriber<MarketData> ping_sub(
      {BenchConfig::IOX_SERVICE, BenchConfig::IOX_INSTANCE,
       BenchConfig::IOX_EVENT_PING},
      iox::popo::SubscriberOptions{.queueCapacity =
                                       BenchConfig::IOX_QUEUE_CAPACITY,
                                   .historyRequest = 0});

  // 创建 Publisher（发送 Pong）
  iox::popo::Publisher<MarketData> pong_pub(
      {BenchConfig::IOX_SERVICE, BenchConfig::IOX_INSTANCE,
       BenchConfig::IOX_EVENT_PONG},
      iox::popo::PublisherOptions{.historyCapacity =
                                      BenchConfig::IOX_HISTORY_CAPACITY});

  std::cout << "[Consumer] Ready to process requests." << std::endl;

  bool keep_running = true;
  while (keep_running) {
    // 等待 Ping
    wait_for_data(ping_sub);

    // 接收并处理
    ping_sub.take().and_then([&](const auto &recv_sample) {
      // 检查终止信号
      if (recv_sample->sequence_id == BenchConfig::SEQ_TERMINATE) {
        std::cout << "[Consumer] Termination signal received. Exiting."
                  << std::endl;
        keep_running = false;
      }

      // 发送 Pong（回显）
      pong_pub.loan()
          .and_then([&](auto &send_sample) {
            send_sample->sequence_id = recv_sample->sequence_id;
            send_sample->timestamp_ns = recv_sample->timestamp_ns;
            send_sample.publish();
          })
          .or_else([](auto &error) {
            std::cerr << "Pong loan failed: " << error << std::endl;
          });
    });
  }

  std::cout << "[Consumer] Shutdown complete." << std::endl;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------
int main(int argc, char **argv) {
  std::cout << "Starting Iceoryx (Iox) Ping-Pong Benchmark..." << std::endl;

  pid_t pid = fork();
  if (pid < 0) {
    std::cerr << "Fork failed!" << std::endl;
    return 1;
  }

  if (pid == 0) {
    // 子进程运行消费者
    run_consumer();
  } else {
    // 父进程运行生产者
    run_producer();
  }

  return 0;
}
