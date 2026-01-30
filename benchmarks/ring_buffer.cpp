#include "common.hpp"
#include "eph/core/ring_buffer.hpp"

#include <iostream>
#include <memory>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

// =============================================================================
// 适配器 (Adapters)
// 将 RingBuffer 的接口适配为 run_producer/run_consumer 需要的 send/receive 语义
// =============================================================================

template <typename T, size_t Capacity>
class RBSender {
public:
  explicit RBSender(std::shared_ptr<RingBuffer<T, Capacity>> buffer)
      : buffer_(std::move(buffer)) {}

  // 阻塞发送
  void send(const T &data) {
    buffer_->push(data);
  }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> buffer_;
};

template <typename T, size_t Capacity>
class RBReceiver {
public:
  explicit RBReceiver(std::shared_ptr<RingBuffer<T, Capacity>> buffer)
      : buffer_(std::move(buffer)) {}

  // 阻塞接收
  void receive(T &out) {
    buffer_->pop(out);
  }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> buffer_;
};

// =============================================================================
// Main Benchmark
// =============================================================================

int main() {
  std::cout << "Starting Thread (RingBuffer) Ping-Pong Benchmark..." << std::endl;

  // 使用默认的配置容量
  constexpr size_t Capacity = BenchConfig::QUEUE_CAPACITY;

  // 1. 创建两个共享的 RingBuffer 实例
  auto p2c_buffer = std::make_shared<RingBuffer<MarketData, Capacity>>();
  auto c2p_buffer = std::make_shared<RingBuffer<MarketData, Capacity>>();

  // 2. 组装 Sender/Receiver
  // p2c: Producer -> Consumer
  RBSender<MarketData, Capacity> p2c_tx(p2c_buffer);
  RBReceiver<MarketData, Capacity> p2c_rx(p2c_buffer);

  // c2p: Consumer -> Producer
  RBSender<MarketData, Capacity> c2p_tx(c2p_buffer);
  RBReceiver<MarketData, Capacity> c2p_rx(c2p_buffer);

  // 3. 启动消费者线程
  // 消费者持有 p2c 的接收端 和 c2p 的发送端
  std::thread consumer_thread([rx = std::move(p2c_rx), tx = std::move(c2p_tx)]() mutable {
    // 绑定到 Consumer 核心 (在 common.hpp 中定义)
    run_consumer(std::move(rx), std::move(tx));
  });

  // 4. 在主线程运行生产者
  run_producer(std::move(p2c_tx), std::move(c2p_rx), "bench_ping_pong_rb");

  // 5. 等待消费者线程结束
  consumer_thread.join();

  return 0;
}
