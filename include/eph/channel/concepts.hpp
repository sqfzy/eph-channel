#pragma once

#include <concepts>
#include <optional>

namespace eph::channel {

// =========================================================
// 语义 1: 队列 (Queue / Stream)
// 特性: FIFO, 强一致性, 不丢数据 (通过背压/阻塞实现)
// =========================================================

template <typename T>
concept Sender = requires(T t, const typename T::DataType &v) {
  typename T::DataType; // 必须定义数据类型别名

  // 阻塞发送：缓冲区满时等待，绝不丢数据
  { t.send(v) } -> std::same_as<void>;

  // 非阻塞发送：缓冲区满时返回 false
  { t.try_send(v) } -> std::same_as<bool>;

  { t.capacity() } -> std::convertible_to<std::size_t>;
};

template <typename T>
concept Receiver = requires(T t, typename T::DataType &v) {
  typename T::DataType;

  // 阻塞接收：缓冲区空时等待
  { t.receive() } -> std::same_as<typename T::DataType>;

  // 非阻塞接收：缓冲区空时返回 false
  { t.try_receive(v) } -> std::same_as<bool>;
  { t.try_receive() } -> std::same_as<std::optional<typename T::DataType>>;
};

// =========================================================
// 语义 2: 快照 (Snapshot / State)
// 特性: 覆盖写入 (Conflation), 最终一致性, 总是读最新值
// =========================================================

template <typename T>
concept Publisher = requires(T t, const typename T::DataType &v) {
  typename T::DataType;

  // 发布：总是覆盖旧值 (Wait-free)
  { t.publish(v) } -> std::same_as<void>;
};

template <typename T>
concept Subscriber = requires(T t, typename T::DataType &v) {
  typename T::DataType;

  // 获取最新快照：非破坏性读取
  { t.fetch() } -> std::same_as<typename T::DataType>;

  // 尝试获取：如果发生写冲突则返回 false
  { t.try_fetch(v) } -> std::same_as<bool>;
};

} // namespace eph::channel
