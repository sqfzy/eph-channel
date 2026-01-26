#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <string>
#include <optional>

namespace shm {

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
requires Shmable<T>
class Sender {
public:
  explicit Sender(std::string name) : shm_(std::move(name), true) {}

  void send(const T &data) { shm_->push_blocking(data); }

  // [[nodiscard]] 提醒用户检查发送结果
  [[nodiscard]] bool try_send(const T &data, int max_retries = 0) {
    return shm_->try_push(data, max_retries);
  }

  size_t size() const noexcept { return shm_->size(); }
  bool is_full() const noexcept { return shm_->full(); }
  static constexpr size_t capacity() noexcept { return Capacity; }
  const std::string &name() const noexcept { return shm_.name(); }

private:
  SharedMemory<RingBuffer<T, Capacity>> shm_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
requires Shmable<T>
class Receiver {
public:
  explicit Receiver(std::string name) : shm_(std::move(name), false) {}

  T receive() { return shm_->pop_blocking(); }
  
  void receive(T& out) { shm_->pop_blocking(out); }

  [[nodiscard]] std::optional<T> try_receive(int max_retries = 0) {
    return shm_->try_pop(max_retries);
  }

  size_t size() const noexcept { return shm_->size(); }
  bool is_empty() const noexcept { return shm_->empty(); }
  static constexpr size_t capacity() noexcept { return Capacity; }
  const std::string &name() const noexcept { return shm_.name(); }

private:
  SharedMemory<RingBuffer<T, Capacity>> shm_;
};

// 工厂函数
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
auto channel(const std::string &name) {
  return std::make_pair(Sender<T, Capacity>(name), Receiver<T, Capacity>(name));
}

} // namespace shm
