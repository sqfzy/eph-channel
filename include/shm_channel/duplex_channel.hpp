#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace shm::ipc {

template <typename T, size_t Capacity> struct DuplexLayout {
  static constexpr size_t RbAlign =
      (alignof(RingBuffer<T, Capacity>) > config::CACHE_LINE_SIZE)
          ? alignof(RingBuffer<T, Capacity>)
          : config::CACHE_LINE_SIZE;

  alignas(RbAlign) RingBuffer<T, Capacity> p2c;
  alignas(RbAlign) RingBuffer<T, Capacity> c2p;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexSender {
public:
  explicit DuplexSender(std::string name) : shm_(std::move(name), true) {}

  T send_receive(const T &request) {
    shm_->p2c.push(request);
    return shm_->c2p.pop();
  }

  [[nodiscard]] std::optional<T> try_send_receive(const T &request) {
    if (!shm_->p2c.try_push(request)) {
      return std::nullopt;
    }
    return shm_->c2p.try_pop();
  }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexReceiver {
public:
  explicit DuplexReceiver(std::string name) : shm_(std::move(name), false) {}

  void receive_send(std::function<T(const T &)> handler) {
    T request;
    shm_->p2c.pop(request); // 使用无需拷贝的 pop 变体
    T response = handler(request);
    shm_->c2p.push(response);
  }

  [[nodiscard]] bool try_receive_send(std::function<T(const T &)> handler) {
    auto req_opt = shm_->p2c.try_pop();
    if (!req_opt)
      return false;

    T response = handler(*req_opt);
    return shm_->c2p.try_push(response);
  }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
auto duplex_channel(const std::string &name) {
  auto sender = DuplexSender<T, Capacity>(name);
  auto receiver = DuplexReceiver<T, Capacity>(name);

  return std::make_pair(std::move(sender), std::move(receiver));
}

} // namespace shm::ipc

namespace shm::itc {

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexSender {
public:
  DuplexSender(std::shared_ptr<RingBuffer<T, Capacity>> p2c,
               std::shared_ptr<RingBuffer<T, Capacity>> c2p)
      : p2c_(std::move(p2c)), c2p_(std::move(c2p)) {}

  T send_receive(const T &request) {
    p2c_->push(request);
    return c2p_->pop();
  }

  [[nodiscard]] std::optional<T> try_send_receive(const T &request) {
    if (!p2c_->try_push(request)) {
      return std::nullopt;
    }
    return c2p_->try_pop();
  }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> p2c_;
  std::shared_ptr<RingBuffer<T, Capacity>> c2p_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexReceiver {
public:
  DuplexReceiver(std::shared_ptr<RingBuffer<T, Capacity>> p2c,
                 std::shared_ptr<RingBuffer<T, Capacity>> c2p)
      : p2c_(std::move(p2c)), c2p_(std::move(c2p)) {}

  void receive_send(std::function<T(const T &)> handler) {
    T request;
    p2c_->pop(request);
    T response = handler(request);
    c2p_->push(response);
  }

  [[nodiscard]] bool try_receive_send(std::function<T(const T &)> handler) {
    T request;
    if (!p2c_->try_pop(request)) {
      return false;
    }
    T response = handler(request);
    c2p_->push(response);
    return true;
  }

private:
  std::shared_ptr<RingBuffer<T, Capacity>> p2c_;
  std::shared_ptr<RingBuffer<T, Capacity>> c2p_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
auto duplex_channel() {
  auto p2c = std::make_shared<RingBuffer<T, Capacity>>();
  auto c2p = std::make_shared<RingBuffer<T, Capacity>>();

  return std::make_pair(DuplexSender<T, Capacity>(p2c, c2p),
                        DuplexReceiver<T, Capacity>(p2c, c2p));
}

} // namespace shm::itc
