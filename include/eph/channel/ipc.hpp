#pragma once

#include "eph/core/ring_buffer.hpp"
#include "eph/core/seq_lock.hpp"
#include "eph/core/seq_lock_buffer.hpp"
#include "eph/core/shared_memory.hpp"
#include <string>

namespace eph::ipc {

// =========================================================
// 1. Queue (基于 RingBuffer + SHM)
// =========================================================

template <typename T, size_t Capacity = 1024> class Sender {
public:
  using DataType = T;
  // is_owner = true
  Sender(std::string name, bool use_huge_pages)
      : shm_(std::move(name), true, use_huge_pages) {}

  // 阻塞发送，绝不丢数据
  void send(const T &data) { shm_->push(data); }
  bool try_send(const T &data) { return shm_->try_push(data); }
  static constexpr size_t capacity() { return Capacity; }

private:
  SharedMemory<RingBuffer<T, Capacity>> shm_;
};

template <typename T, size_t Capacity = 1024> class Receiver {
public:
  using DataType = T;
  // is_owner = false
  Receiver(std::string name, bool use_huge_pages)
      : shm_(std::move(name), false, use_huge_pages) {}

  T receive() { return shm_->pop(); }
  void receive(T &out) { shm_->pop(out); }
  bool try_receive(T &out) { return shm_->try_pop(out); }
  std::optional<T> try_receive() { return shm_->try_pop(); }
  static constexpr size_t capacity() { return Capacity; }

private:
  SharedMemory<RingBuffer<T, Capacity>> shm_;
};

// 工厂：IPC 队列
template <typename T, size_t Capacity = 1024>
auto make_queue(const std::string &name, bool use_huge_pages = false) {
  return std::make_pair(Sender<T, Capacity>(name, use_huge_pages),
                        Receiver<T, Capacity>(name, use_huge_pages));
}

// =========================================================
// 2. Snapshot (基于 SeqLock + SHM)
// =========================================================

template <typename T> class Publisher {
public:
  using DataType = T;
  Publisher(std::string name, bool use_huge_pages)
      : shm_(std::move(name), true, use_huge_pages) {}

  void publish(const T &data) { shm_->store(data); }

  template <typename F>
    requires std::invocable<F, T &>
  void publish(F &&writer) {
    shm_->write(std::forward<F>(writer));
  }

private:
  SharedMemory<SeqLock<T>> shm_;
};

template <typename T> class Subscriber {
public:
  using DataType = T;
  Subscriber(std::string name, bool use_huge_pages)
      : shm_(std::move(name), false, use_huge_pages) {}

  T fetch() { return shm_->load(); }
  bool try_fetch(T &out) { return shm_->try_load(out); }

  template <typename F>
    requires std::invocable<F, T &>
  void fetch(F &&visitor) {
    shm_->read(std::forward<F>(visitor));
  }

private:
  SharedMemory<SeqLock<T>> shm_;
};

// 工厂：IPC 快照
template <typename T>
auto make_snapshot(const std::string &name, bool use_huge_pages = false) {
  return std::make_pair(Publisher<T>(name, use_huge_pages),
                        Subscriber<T>(name, use_huge_pages));
}

// =========================================================
// 3. Buffered Snapshot (基于 SeqLockBuffer + SHM)
// =========================================================

template <typename T, size_t N = 8> class BufferedPublisher {
public:
  using DataType = T;
  BufferedPublisher(std::string name, bool use_huge_pages)
      : shm_(std::move(name), true, use_huge_pages) {}

  void publish(const T &data) { shm_->store(data); }

  template <typename F>
    requires std::invocable<F, T &>
  void publish(F &&writer) {
    shm_->write(std::forward<F>(writer));
  }

private:
  SharedMemory<SeqLockBuffer<T, N>> shm_;
};

template <typename T, size_t N = 8> class BufferedSubscriber {
public:
  using DataType = T;
  BufferedSubscriber(std::string name, bool use_huge_pages)
      : shm_(std::move(name), false, use_huge_pages) {}

  T fetch() { return shm_->load(); }
  bool try_fetch(T &out) { return shm_->try_load(out); }

  template <typename F>
    requires std::invocable<F, T &>
  void fetch(F &&visitor) {
    while (!shm_->try_read(visitor)) {
      eph::cpu_relax();
    }
  }

private:
  SharedMemory<SeqLockBuffer<T, N>> shm_;
};

// 工厂：IPC 缓冲快照
template <typename T, size_t N = 8>
auto make_buffered_snapshot(const std::string &name,
                            bool use_huge_pages = false) {
  return std::make_pair(BufferedPublisher<T, N>(name, use_huge_pages),
                        BufferedSubscriber<T, N>(name, use_huge_pages));
}

} // namespace eph::ipc
