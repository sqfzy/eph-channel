#pragma once

#include "eph/core/ring_buffer.hpp"
#include "eph/core/seq_lock.hpp"
#include "eph/core/seq_lock_buffer.hpp"
#include <memory>

namespace eph::itc {

// =========================================================
// 1. Queue (基于 RingBuffer)
// =========================================================

template <typename T, size_t Capacity = 1024> class Sender {
public:
  using DataType = T;
  explicit Sender(std::shared_ptr<RingBuffer<T, Capacity>> buf)
      : buffer_(std::move(buf)) {}

  // 阻塞发送，绝不丢数据 (Backpressure)
  void send(const T &data) { buffer_->push(data); }

  // 非阻塞发送
  bool try_send(const T &data) { return buffer_->try_push(data); }

  static constexpr size_t capacity() { return Capacity; }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> buffer_;
};

template <typename T, size_t Capacity = 1024> class Receiver {
public:
  using DataType = T;
  explicit Receiver(std::shared_ptr<RingBuffer<T, Capacity>> buf)
      : buffer_(std::move(buf)) {}

  T receive() { return buffer_->pop(); }
  void receive(T &out) { buffer_->pop(out); }
  bool try_receive(T &out) { return buffer_->try_pop(out); }
  std::optional<T> try_receive() { return buffer_->try_pop(); }
  static constexpr size_t capacity() { return Capacity; }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> buffer_;
};

// 工厂：创建队列通道
template <typename T, size_t Capacity = 1024> auto make_queue() {
  auto buffer = std::make_shared<RingBuffer<T, Capacity>>();
  return std::make_pair(Sender<T, Capacity>(buffer),
                        Receiver<T, Capacity>(buffer));
}

// =========================================================
// 2. Snapshot (基于 SeqLock)
// 适合：低频更新 (配置、状态机)
// =========================================================

template <typename T> class Publisher {
public:
  using DataType = T;
  explicit Publisher(std::shared_ptr<SeqLock<T>> impl)
      : impl_(std::move(impl)) {}

  void publish(const T &data) { impl_->store(data); }

  template <typename F>
    requires std::invocable<F, T &>
  void publish(F &&writer) {
    impl_->write(std::forward<F>(writer));
  }

private:
  std::shared_ptr<SeqLock<T>> impl_;
};

template <typename T> class Subscriber {
public:
  using DataType = T;
  explicit Subscriber(std::shared_ptr<SeqLock<T>> impl)
      : impl_(std::move(impl)) {}

  T fetch() { return impl_->load(); }
  bool try_fetch(T &out) { return impl_->try_load(out); }

  template <typename F>
    requires std::invocable<F, T &>
  void fetch(F &&visitor) {
    impl_->read(std::forward<F>(visitor));
  }

private:
  std::shared_ptr<SeqLock<T>> impl_;
};

// 工厂：创建普通快照通道
template <typename T> auto make_snapshot() {
  auto impl = std::make_shared<SeqLock<T>>();
  return std::make_pair(Publisher<T>(impl), Subscriber<T>(impl));
}

// =========================================================
// 3. Buffered Snapshot (基于 SeqLockBuffer)
// 适合：高频更新 (行情、传感器数据)，防止 Cache Thrashing
// =========================================================

template <typename T, size_t N = 8> class BufferedPublisher {
public:
  using DataType = T;
  explicit BufferedPublisher(std::shared_ptr<SeqLockBuffer<T, N>> impl)
      : impl_(std::move(impl)) {}

  void publish(const T &data) { impl_->store(data); }

  template <typename F>
    requires std::invocable<F, T &>
  void publish(F &&writer) {
    impl_->write(std::forward<F>(writer));
  }

private:
  std::shared_ptr<SeqLockBuffer<T, N>> impl_;
};

template <typename T, size_t N = 8> class BufferedSubscriber {
public:
  using DataType = T;
  explicit BufferedSubscriber(std::shared_ptr<SeqLockBuffer<T, N>> impl)
      : impl_(std::move(impl)) {}

  T fetch() { return impl_->load(); }
  bool try_fetch(T &out) { return impl_->try_load(out); }

  template <typename F>
    requires std::invocable<F, T &>
  void fetch(F &&visitor) {
    // 手动自旋实现阻塞式 Visitor
    while (!impl_->try_read(visitor)) {
      eph::cpu_relax();
    }
  }

private:
  std::shared_ptr<SeqLockBuffer<T, N>> impl_;
};

// 工厂：创建缓冲快照通道
template <typename T, size_t N = 8> auto make_buffered_snapshot() {
  auto impl = std::make_shared<SeqLockBuffer<T, N>>();
  return std::make_pair(BufferedPublisher<T, N>(impl),
                        BufferedSubscriber<T, N>(impl));
}

} // namespace eph::itc
