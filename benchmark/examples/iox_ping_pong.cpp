#include "benchmark/config.hpp"
#include "benchmark/stats.hpp"
#include "benchmark/system.hpp"
#include "benchmark/timer.hpp"

#include <iceoryx_posh/popo/publisher.hpp>
#include <iceoryx_posh/popo/subscriber.hpp>
#include <iceoryx_posh/runtime/posh_runtime.hpp>

#include <iostream>
#include <string_view>

using namespace benchmark;

// -----------------------------------------------------------------------------
// 辅助函数：等待数据可用
// -----------------------------------------------------------------------------
template <typename T> void wait_for_data(iox::popo::Subscriber<T> &sub) {
  while (!sub.hasData()) {
    // 自旋等待
    std::this_thread::yield();
  }
}

// -----------------------------------------------------------------------------
// Producer（发送 Ping，接收 Pong）
// -----------------------------------------------------------------------------
void run_producer() {
  System::pin_to_core(BenchConfig::PRODUCER_CORE);
  System::set_realtime_priority();

  // 初始化 Iceoryx 运行时
  iox::runtime::PoshRuntime::initRuntime(BenchConfig::IOX_APP_NAME_PRODUCER);

  // 创建 Publisher（发送 Ping）
  iox::popo::Publisher<MarketData> ping_pub(
      {BenchConfig::IOX_SERVICE, BenchConfig::IOX_INSTANCE,
       BenchConfig::IOX_EVENT_PING},
      iox::popo::PublisherOptions{.historyCapacity =
                                      BenchConfig::IOX_HISTORY_CAPACITY});

  // 创建 Subscriber（接收 Pong）
  iox::popo::Subscriber<MarketData> pong_sub(
      {BenchConfig::IOX_SERVICE, BenchConfig::IOX_INSTANCE,
       BenchConfig::IOX_EVENT_PONG},
      iox::popo::SubscriberOptions{.queueCapacity =
                                       BenchConfig::IOX_QUEUE_CAPACITY,
                                   .historyRequest = 0});

  TSCClock clock;
  StatsRecorder stats;
  stats.reserve(BenchConfig::ITERATIONS);

  // 等待订阅者连接
  std::cout << "[Producer] Waiting for consumer..." << std::endl;
  while (!ping_pub.hasSubscribers()) {
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  // 预热
  std::cout << "[Producer] Warmup (" << BenchConfig::WARMUP_ITERATIONS
            << " iterations)..." << std::endl;
  for (int i = 0; i < BenchConfig::WARMUP_ITERATIONS; ++i) {
    ping_pub.loan()
        .and_then([&](auto &sample) {
          sample->sequence_id = 0;
          sample->timestamp_ns = 0;
          sample.publish();
        })
        .or_else([](auto &error) {
          std::cerr << "Warmup loan failed: " << error << std::endl;
        });

    wait_for_data(pong_sub);
    pong_sub.take().and_then([](const auto &) {
      // 丢弃预热数据
    });
  }

  // 正式测试
  std::cout << "[Producer] Running benchmark (" << BenchConfig::ITERATIONS
            << " iterations)..." << std::endl;

  for (int i = 0; i < BenchConfig::ITERATIONS; ++i) {
    uint64_t t0 = TSCClock::now();

    // 发送 Ping
    ping_pub.loan()
        .and_then([&](auto &sample) {
          sample->sequence_id = i + 1;
          sample->timestamp_ns = t0;
          sample.publish();
        })
        .or_else([](auto &error) {
          std::cerr << "Loan failed: " << error << std::endl;
          exit(1);
        });

    // 等待并接收 Pong
    wait_for_data(pong_sub);

    pong_sub.take()
        .and_then([&](const auto &sample) {
          uint64_t t1 = TSCClock::now();

          // 验证序列号
          if (sample->sequence_id != static_cast<uint64_t>(i + 1)) {
            std::cerr << "Sequence mismatch! Expected " << (i + 1) << ", got "
                      << sample->sequence_id << std::endl;
            exit(1);
          }

          // 记录单向延迟（RTT / 2）
          stats.add(i, clock.to_ns(t1 - t0) / 2.0);
        })
        .or_else([](auto &error) {
          std::cerr << "Take failed: " << error << std::endl;
          exit(1);
        });
  }

  // 发送终止信号
  std::cout << "[Producer] Sending termination signal..." << std::endl;
  ping_pub.loan().and_then([&](auto &sample) {
    sample->sequence_id = BenchConfig::SEQ_TERMINATE;
    sample.publish();
  });

  // 等待确认
  wait_for_data(pong_sub);
  pong_sub.take().and_then([](const auto &sample) {
    if (sample->sequence_id == BenchConfig::SEQ_TERMINATE) {
      std::cout << "[Producer] Consumer acknowledged termination." << std::endl;
    }
  });

  // 输出统计报告
  stats.report("iox_latency");
}

// -----------------------------------------------------------------------------
// Consumer（接收 Ping，发送 Pong）
// -----------------------------------------------------------------------------
void run_consumer() {
  System::pin_to_core(BenchConfig::CONSUMER_CORE);
  System::set_realtime_priority();

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
  if (argc < 2) {
    std::cerr << "Usage: " << argv[0] << " [producer|consumer]" << std::endl;
    std::cerr << "\nMake sure RouDi is running before starting:" << std::endl;
    std::cerr << "  iox-roudi" << std::endl;
    return 1;
  }

  std::string_view mode = argv[1];

  if (mode == "producer") {
    run_producer();
  } else if (mode == "consumer") {
    run_consumer();
  } else {
    std::cerr << "Unknown mode: " << mode << std::endl;
    std::cerr << "Valid modes: producer, consumer" << std::endl;
    return 1;
  }

  return 0;
}
