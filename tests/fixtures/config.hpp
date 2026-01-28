#pragma once

#include <atomic>
#include <chrono>
#include <cstring>
#include <string>
#include <random>

namespace eph::test {

// 测试配置常量
struct TestConfig {
  // 超时配置
  static constexpr auto SHORT_TIMEOUT = std::chrono::milliseconds(100);
  static constexpr auto MEDIUM_TIMEOUT = std::chrono::milliseconds(500);
  static constexpr auto LONG_TIMEOUT = std::chrono::seconds(5);

  // 测试数据量
  static constexpr int SMALL_DATA_SIZE = 100;
  static constexpr int MEDIUM_DATA_SIZE = 5000;
  static constexpr int LARGE_DATA_SIZE = 100000;

  // 并发测试
  static constexpr int NUM_THREADS = 4;
  static constexpr int STRESS_ITERATIONS = 1000000;

  // SHM 命名前缀
  static inline const std::string SHM_PREFIX = "/test_eph_";
};

// 生成唯一的共享内存名称
inline std::string generate_unique_shm_name(const std::string& prefix = "test") {
  static std::atomic<int> counter{0};
  auto now = std::chrono::steady_clock::now().time_since_epoch().count();
  return TestConfig::SHM_PREFIX + prefix + "_" + 
         std::to_string(now) + "_" + std::to_string(counter++);
}

// 测试数据结构
struct alignas(64) TestMessage {
  uint64_t id;
  uint64_t timestamp;
  double value;
  char padding[40];
  
  bool operator==(const TestMessage& other) const {
    return id == other.id && timestamp == other.timestamp && value == other.value;
  }
};

struct alignas(128) LargeTestData {
  uint64_t sequence;
  char payload[120];
  
  bool operator==(const LargeTestData& other) const {
    return sequence == other.sequence && 
           std::memcmp(payload, other.payload, sizeof(payload)) == 0;
  }
};

// 随机数据生成器
class TestDataGenerator {
  std::mt19937 rng_{std::random_device{}()};
  std::uniform_int_distribution<uint64_t> dist_{0, UINT64_MAX};

public:
  TestMessage generate_message(uint64_t id = 0) {
    TestMessage msg{};
    msg.id = id ? id : dist_(rng_);
    msg.timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    msg.value = static_cast<double>(dist_(rng_)) / UINT64_MAX;
    return msg;
  }

  LargeTestData generate_large_data(uint64_t seq = 0) {
    LargeTestData data{};
    data.sequence = seq ? seq : dist_(rng_);
    for (size_t i = 0; i < sizeof(data.payload); ++i) {
      data.payload[i] = static_cast<char>(dist_(rng_) % 256);
    }
    return data;
  }
};

} // namespace eph::test
