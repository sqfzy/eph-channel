#pragma once

#include <cstddef>
#include <cstdint>

namespace benchmark {

// -----------------------------------------------------------------------------
// 通用基准测试配置
// -----------------------------------------------------------------------------
struct BenchConfig {
  // 测试参数
  static constexpr int ITERATIONS = 1000000;      // 测试迭代次数
  static constexpr int WARMUP_ITERATIONS = 10000; // 预热次数
  
  // CPU 绑定
  static constexpr int PRODUCER_CORE = 2;
  static constexpr int CONSUMER_CORE = 4;
  
  // 终止信号
  static constexpr uint64_t SEQ_TERMINATE = ~0ULL;
  
  // 共享内存配置
  static constexpr const char* SHM_NAME = "/bench_ping_pong";
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

} // namespace benchmark
