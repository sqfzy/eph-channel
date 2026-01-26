#pragma once

#include "ring_buffer.hpp"
#include "shared_memory.hpp"
#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <thread>

namespace shm::ipc {

template <typename T, size_t Capacity> struct DuplexLayout {
  static constexpr size_t RbAlign =
      (alignof(RingBuffer<T, Capacity>) > config::CACHE_LINE_SIZE)
          ? alignof(RingBuffer<T, Capacity>)
          : config::CACHE_LINE_SIZE;

  alignas(RbAlign) RingBuffer<T, Capacity> p2c; // Client -> Server
  alignas(RbAlign) RingBuffer<T, Capacity> c2p; // Server -> Client
};

// --------------------------------------------------------------------------
// DuplexSender (Client Side)
// --------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexSender {
public:
  explicit DuplexSender(std::string name) : shm_(std::move(name), true) {}

  // --- 同步 RPC ---
  T send_receive(const T &request) {
    shm_->p2c.push(request);
    return shm_->c2p.pop();
  }

  // --- 超时 RPC ---
  template <class Rep, class Period>
  std::optional<T>
  send_receive(const T &request,
               const std::chrono::duration<Rep, Period> &timeout) {
    auto start = std::chrono::steady_clock::now();
    
    // 发送
    while (!shm_->p2c.try_push(request)) {
      if (std::chrono::steady_clock::now() - start > timeout)
        return std::nullopt;
      std::this_thread::yield();
    }

    // 接收 (剩余时间)
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto remaining = timeout - elapsed;
    if (remaining <= std::chrono::nanoseconds::zero())
      return std::nullopt;

    T response;
    auto wait_start = std::chrono::steady_clock::now();
    while (!shm_->c2p.try_pop(response)) {
      if (std::chrono::steady_clock::now() - wait_start > remaining)
        return std::nullopt;
      std::this_thread::yield();
    }
    return response;
  }

  // --- 解耦/异步接口 ---
  void send_request(const T &req) { shm_->p2c.push(req); }
  bool try_send_request(const T &req) { return shm_->p2c.try_push(req); }

  T receive_response() { return shm_->c2p.pop(); }
  std::optional<T> try_receive_response() { return shm_->c2p.try_pop(); }

  // 非阻塞 send_receive (即发即收，通常用于 response 已就绪的场景)
  [[nodiscard]] std::optional<T> try_send_receive(const T &request) {
    if (!shm_->p2c.try_push(request)) {
      return std::nullopt;
    }
    return shm_->c2p.try_pop();
  }

private:
  SharedMemory<DuplexLayout<T, Capacity>> shm_;
};

// --------------------------------------------------------------------------
// DuplexReceiver (Server Side)
// --------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexReceiver {
public:
  explicit DuplexReceiver(std::string name) : shm_(std::move(name), false) {}

  // --- 处理循环 ---
  void receive_send(std::function<T(const T &)> handler) {
    T request;
    shm_->p2c.pop(request);
    T response = handler(request);
    shm_->c2p.push(response);
  }

  // --- 超时处理 ---
  template <class Rep, class Period>
  bool receive_send(std::function<T(const T &)> handler,
                    const std::chrono::duration<Rep, Period> &timeout) {
    T request;
    auto start = std::chrono::steady_clock::now();
    
    // 等待请求
    while (!shm_->p2c.try_pop(request)) {
      if (std::chrono::steady_clock::now() - start > timeout)
        return false;
      std::this_thread::yield();
    }

    // 处理 & 回复 (尽量不超时发送)
    T response = handler(request);
    shm_->c2p.push(response);
    return true;
  }

  // --- 解耦/异步接口 ---
  T receive_request() { return shm_->p2c.pop(); }
  std::optional<T> try_receive_request() { return shm_->p2c.try_pop(); }

  void send_response(const T &resp) { shm_->c2p.push(resp); }
  bool try_send_response(const T &resp) { return shm_->c2p.try_push(resp); }

  [[nodiscard]] bool try_receive_send(std::function<T(const T &)> handler) {
    auto req_opt = shm_->p2c.try_pop();
    if (!req_opt) return false;
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

// --------------------------------------------------------------------------
// ITC DuplexSender
// --------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexSender {
public:
  DuplexSender(std::shared_ptr<RingBuffer<T, Capacity>> p2c,
               std::shared_ptr<RingBuffer<T, Capacity>> c2p)
      : p2c_(std::move(p2c)), c2p_(std::move(c2p)) {}

  // --- 同步 RPC ---
  T send_receive(const T &request) {
    p2c_->push(request);
    return c2p_->pop();
  }

  // --- 超时 RPC ---
  template <class Rep, class Period>
  std::optional<T>
  send_receive(const T &request,
               const std::chrono::duration<Rep, Period> &timeout) {
    auto start = std::chrono::steady_clock::now();
    
    // 发送
    while (!p2c_->try_push(request)) {
      if (std::chrono::steady_clock::now() - start > timeout)
        return std::nullopt;
      std::this_thread::yield();
    }

    // 接收
    auto elapsed = std::chrono::steady_clock::now() - start;
    auto remaining = timeout - elapsed;
    if (remaining <= std::chrono::nanoseconds::zero())
      return std::nullopt;

    T response;
    auto wait_start = std::chrono::steady_clock::now();
    while (!c2p_->try_pop(response)) {
      if (std::chrono::steady_clock::now() - wait_start > remaining)
        return std::nullopt;
      std::this_thread::yield();
    }
    return response;
  }

  // --- 解耦/异步接口 ---
  void send_request(const T &req) { p2c_->push(req); }
  bool try_send_request(const T &req) { return p2c_->try_push(req); }

  T receive_response() { return c2p_->pop(); }
  std::optional<T> try_receive_response() { return c2p_->try_pop(); }
  
  // 非阻塞 send_receive (即发即收，通常用于 response 已就绪的场景)
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

// --------------------------------------------------------------------------
// ITC DuplexReceiver
// --------------------------------------------------------------------------
template <typename T, size_t Capacity = config::DEFAULT_CAPACITY>
  requires ShmData<T>
class DuplexReceiver {
public:
  DuplexReceiver(std::shared_ptr<RingBuffer<T, Capacity>> p2c,
                 std::shared_ptr<RingBuffer<T, Capacity>> c2p)
      : p2c_(std::move(p2c)), c2p_(std::move(c2p)) {}

  // --- 处理循环 ---
  void receive_send(std::function<T(const T &)> handler) {
    T request;
    p2c_->pop(request);
    T response = handler(request);
    c2p_->push(response);
  }

  // --- 超时处理 ---
  template <class Rep, class Period>
  bool receive_send(std::function<T(const T &)> handler,
                    const std::chrono::duration<Rep, Period> &timeout) {
    T request;
    auto start = std::chrono::steady_clock::now();
    while (!p2c_->try_pop(request)) {
      if (std::chrono::steady_clock::now() - start > timeout)
        return false;
      std::this_thread::yield();
    }

    T response = handler(request);
    c2p_->push(response);
    return true;
  }

  // --- 解耦/异步接口 ---
  T receive_request() { return p2c_->pop(); }
  std::optional<T> try_receive_request() { return p2c_->try_pop(); }

  void send_response(const T &resp) { c2p_->push(resp); }
  bool try_send_response(const T &resp) { return c2p_->try_push(resp); }

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
