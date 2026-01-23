#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <functional>
#include <string>

namespace shm {

// 双向通道的共享内存布局
template <typename T, size_t Capacity> struct DuplexLayout {
  alignas(128) RingBuffer<T, Capacity> p2c; // Producer -> Consumer
  alignas(128) RingBuffer<T, Capacity> c2p; // Consumer -> Producer
};

// -----------------------------------------------------------------------------
// DuplexSender - 双向通道的发送端（发起方）
// -----------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
class DuplexSender {
public:
  explicit DuplexSender(const std::string &name)
      : shm_(name, true) {} // 创建共享内存

  // 发送请求并等待响应
  T send_receive(const T &request) {
    shm_->p2c.push_blocking(request);
    T response;
    shm_->c2p.pop_blocking(response);
    return response;
  }

  // 非阻塞版本
  bool try_send_receive(const T &request, T &response, int max_retries = 100) {
    if (!shm_->p2c.try_push(request, max_retries))
      return false;
    return shm_->c2p.try_pop(response, max_retries);
  }

  // 握手同步
  void handshake() {
    T dummy{};
    send_receive(dummy);
  }

  constexpr size_t capacity() const noexcept { return Capacity; }
  const std::string &name() const noexcept { return shm_.name(); }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

// -----------------------------------------------------------------------------
// DuplexReceiver - 双向通道的接收端（响应方）
// -----------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
class DuplexReceiver {
public:
  explicit DuplexReceiver(const std::string &name)
      : shm_(name, false) {} // 连接到已有共享内存

  // 接收请求并发送响应
  void receive_send(std::function<T(const T &)> handler) {
    T request;
    shm_->p2c.pop_blocking(request);
    T response = handler(request);
    shm_->c2p.push_blocking(response);
  }

  // 分步操作：先接收
  T receive_request() {
    T request;
    shm_->p2c.pop_blocking(request);
    return request;
  }

  // 分步操作：后发送
  void send_response(const T &response) { shm_->c2p.push_blocking(response); }

  // 非阻塞版本
  bool try_receive_send(std::function<T(const T &)> handler,
                        int max_retries = 100) {
    T request;
    if (!shm_->p2c.try_pop(request, max_retries))
      return false;
    T response = handler(request);
    return shm_->c2p.try_push(response, max_retries);
  }

  // 握手同步
  void handshake() {
    receive_send([](const T &) { return T{}; });
  }

  constexpr size_t capacity() const noexcept { return Capacity; }
  const std::string &name() const noexcept { return shm_.name(); }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

// -----------------------------------------------------------------------------
// 创建双向 channel 的便捷函数
// -----------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
std::pair<DuplexSender<T, Capacity>, DuplexReceiver<T, Capacity>>
duplex_channel(const std::string &name) {
  return {DuplexSender<T, Capacity>(name), DuplexReceiver<T, Capacity>(name)};
}

} // namespace shm
