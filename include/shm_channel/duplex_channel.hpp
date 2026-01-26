#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <functional>
#include <string>
#include <optional>

namespace shm {

template <typename T, size_t Capacity>
struct DuplexLayout {
  alignas(config::CACHE_LINE_SIZE) RingBuffer<T, Capacity> p2c; 
  alignas(config::CACHE_LINE_SIZE) RingBuffer<T, Capacity> c2p; 
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
requires Shmable<T>
class DuplexSender {
public:
  explicit DuplexSender(std::string name) : shm_(std::move(name), true) {}

  T send_receive(const T &request) {
    shm_->p2c.push_blocking(request);
    return shm_->c2p.pop_blocking();
  }

  [[nodiscard]] std::optional<T> try_send_receive(const T &request, int max_retries = 100) {
    if (!shm_->p2c.try_push(request, max_retries)) {
        return std::nullopt;
    }
    return shm_->c2p.try_pop(max_retries);
  }

  void handshake() {
    T dummy{};
    send_receive(dummy);
  }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
requires Shmable<T>
class DuplexReceiver {
public:
  explicit DuplexReceiver(std::string name) : shm_(std::move(name), false) {}

  void receive_send(std::function<T(const T &)> handler) {
    T request;
    shm_->p2c.pop_blocking(request); // 使用无需拷贝的 pop 变体
    T response = handler(request);
    shm_->c2p.push_blocking(response);
  }

  [[nodiscard]] bool try_receive_send(std::function<T(const T &)> handler, int max_retries = 100) {
    auto req_opt = shm_->p2c.try_pop(max_retries);
    if (!req_opt) return false;
    
    T response = handler(*req_opt);
    return shm_->c2p.try_push(response, max_retries);
  }

  void handshake() {
    receive_send([](const T &) { return T{}; });
  }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

} // namespace shm
