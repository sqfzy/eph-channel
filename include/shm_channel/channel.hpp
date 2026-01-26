#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <memory>
#include <optional>
#include <string>

namespace shm::ipc {

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class Sender {
public:
  explicit Sender(std::string name) : shm_(std::move(name), true) {}

  void send(const T &data) { shm_->push(data); }

  [[nodiscard]] bool try_send(const T &data) { return shm_->try_push(data); }

  size_t size() const noexcept { return shm_->size(); }
  bool is_full() const noexcept { return shm_->full(); }
  static constexpr size_t capacity() noexcept { return Capacity; }
  const std::string &name() const noexcept { return shm_.name(); }

private:
  SharedMemory<RingBuffer<T, Capacity>> shm_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class Receiver {
public:
  explicit Receiver(std::string name) : shm_(std::move(name), false) {}

  T receive() { return shm_->pop(); }

  void receive(T &out) { shm_->pop(out); }

  [[nodiscard]] bool try_receive(T &out) { return shm_->try_pop(out); }

  std::optional<T> try_receive() { return shm_->try_pop(); }

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
  // 1. 显式先构造 Sender (Owner)，它负责创建共享内存文件
  Sender<T, Capacity> sender(name);

  // 2. 后构造 Receiver (User)，此时文件已存在
  Receiver<T, Capacity> receiver(name);

  // 3. 移动所有权并返回
  return std::make_pair(std::move(sender), std::move(receiver));
}
} // namespace shm::ipc



namespace shm::itc {

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class Sender {
public:
  // 接管 RingBuffer 的共享所有权
  explicit Sender(std::shared_ptr<RingBuffer<T, Capacity>> buffer)
      : buffer_(std::move(buffer)) {}

  void send(const T &data) { buffer_->push(data); }

  [[nodiscard]] bool try_send(const T &data) { return buffer_->try_push(data); }

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

  T receive() { return buffer_->pop(); }

  void receive(T &out) { buffer_->pop(out); }

  [[nodiscard]] bool try_receive(T &out) { return buffer_->try_pop(out); }

  std::optional<T> try_receive() { return buffer_->try_pop(); }

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

} // namespace shm::thread
