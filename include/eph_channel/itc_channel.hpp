#pragma once

#include "ring_buffer.hpp"
#include <chrono>
#include <memory>
#include <optional>

namespace eph::itc {

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class Sender {
public:
  explicit Sender(std::shared_ptr<RingBuffer<T, Capacity>> buffer)
      : buffer_(std::move(buffer)) {}

  // --- 基础接口 ---
  void send(const T &data) { buffer_->push(data); }
  [[nodiscard]] bool try_send(const T &data) { return buffer_->try_push(data); }

  // --- 超时接口 ---
  template <class Rep, class Period>
  bool send(const T &data, const std::chrono::duration<Rep, Period> &timeout) {
    auto start = std::chrono::steady_clock::now();
    while (!try_send(data)) {
      if (std::chrono::steady_clock::now() - start > timeout)
        return false;
      cpu_relax();
    }
    return true;
  }

  template <class Clock, class Duration>
  bool send(const T &data,
            const std::chrono::time_point<Clock, Duration> &deadline) {
    while (!try_send(data)) {
      if (Clock::now() >= deadline)
        return false;
      cpu_relax();
    }
    return true;
  }

  // --- 批量接口 ---
  template <typename InputIt> size_t send_batch(InputIt first, InputIt last) {
    size_t count = 0;
    while (first != last) {
      if (!try_send(*first))
        break;
      ++first;
      ++count;
    }
    return count;
  }

  // --- 状态查询 ---
  size_t size() const noexcept { return buffer_->size(); }
  bool is_full() const noexcept { return buffer_->full(); }
  static constexpr size_t capacity() noexcept { return Capacity; }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> buffer_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class Receiver {
public:
  explicit Receiver(std::shared_ptr<RingBuffer<T, Capacity>> buffer)
      : buffer_(std::move(buffer)) {}

  // --- 基础接口 ---
  T receive() { return buffer_->pop(); }
  void receive(T &out) { buffer_->pop(out); }
  [[nodiscard]] bool try_receive(T &out) { return buffer_->try_pop(out); }
  std::optional<T> try_receive() { return buffer_->try_pop(); }

  // --- 超时接口 ---
  template <class Rep, class Period>
  bool receive(T &out, const std::chrono::duration<Rep, Period> &timeout) {
    auto start = std::chrono::steady_clock::now();
    while (!try_receive(out)) {
      if (std::chrono::steady_clock::now() - start > timeout)
        return false;
      cpu_relax();
    }
    return true;
  }

  template <class Clock, class Duration>
  std::optional<T>
  receive(const std::chrono::time_point<Clock, Duration> &deadline) {
    T out;
    while (!try_receive(out)) {
      if (Clock::now() >= deadline)
        return std::nullopt;
      cpu_relax();
    }
    return out;
  }

  // --- 批量接口 ---
  template <typename OutputIt>
  size_t receive_batch(OutputIt d_first, size_t max_count) {
    size_t count = 0;
    T temp;
    while (count < max_count && try_receive(temp)) {
      *d_first++ = temp;
      ++count;
    }
    return count;
  }

  // --- 状态查询 ---
  size_t size() const noexcept { return buffer_->size(); }
  bool is_empty() const noexcept { return buffer_->empty(); }
  static constexpr size_t capacity() noexcept { return Capacity; }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> buffer_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
auto channel() {
  auto buffer = std::make_shared<RingBuffer<T, Capacity>>();
  return std::make_pair(Sender<T, Capacity>(buffer),
                        Receiver<T, Capacity>(buffer));
}

}
