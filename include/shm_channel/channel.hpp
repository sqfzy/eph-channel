#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <string>

namespace shm {

// -----------------------------------------------------------------------------
// Sender
// -----------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY> class Sender {
  SharedMemory<RingBuffer<T, Capacity>> shm_;

public:
  explicit Sender(const std::string &name) : shm_(name, true) {} // 创建共享内存

  // 阻塞发送
  void send(const T &data) { shm_->push_blocking(data); }

  // 非阻塞发送
  bool try_send(const T &data, int max_retries = 0) {
    return shm_->try_push(data, max_retries);
  }

  // 获取容量
  constexpr size_t capacity() const noexcept { return Capacity; }

  // 获取当前队列大小（近似值）
  size_t size() const noexcept { return shm_->size(); }

  // 检查是否已满
  bool is_full() const noexcept { return shm_->full(); }

  const std::string &name() const noexcept { return shm_.name(); }
};

// -----------------------------------------------------------------------------
// Receiver
// -----------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
class Receiver {
  SharedMemory<RingBuffer<T, Capacity>> shm_;

public:
  explicit Receiver(const std::string &name)
      : shm_(name, false) {} // 连接到已有共享内存

  // 阻塞接收
  T receive() {
    T data;
    shm_->pop_blocking(data);
    return data;
  }

  // 阻塞接收（避免拷贝）
  void receive(T &out_data) { shm_->pop_blocking(out_data); }

  // 非阻塞接收
  bool try_receive(T &out_data, int max_retries = 0) {
    return shm_->try_pop(out_data, max_retries);
  }

  // 获取容量
  constexpr size_t capacity() const noexcept { return Capacity; }

  // 获取当前队列大小（近似值）
  size_t size() const noexcept { return shm_->size(); }

  // 检查是否为空
  bool is_empty() const noexcept { return shm_->empty(); }

  const std::string &name() const noexcept { return shm_.name(); }
};

// -----------------------------------------------------------------------------
// 创建 channel 的便捷函数
// -----------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
std::pair<Sender<T, Capacity>, Receiver<T, Capacity>>
channel(const std::string &name) {
  return {Sender<T, Capacity>(name), Receiver<T, Capacity>(name)};
}

} // namespace shm
