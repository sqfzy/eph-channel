#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <functional>
#include <optional>
#include <string>

namespace shm {

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

  void handshake() {
    T dummy{};
    send_receive(dummy);
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

  void handshake() {
    receive_send([](const T &) { return T{}; });
  }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

} // namespace shm
