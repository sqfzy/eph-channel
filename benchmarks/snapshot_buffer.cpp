#include "common.hpp"
#include "eph/benchmark/recorder.hpp"
#include "eph/benchmark/timer.hpp"
#include "eph/core/seq_lock_buffer.hpp"
#include <format>
#include <print>
#include <thread>
#include <utility>

using namespace eph;
using namespace eph::benchmark;

template <typename T, size_t N>
Stats bench_store(std::string name, SeqLockBuffer<T, N> &buffer,
                  const T &data) {
  bind_cpu(3);

  return run_bench(name, [&] { buffer.store(data); },
                   {
                       .limit = BenchConfig::DURATION_SEC,
                   });
}

template <typename T, size_t N>
Stats bench_store_and_load(std::string name, SeqLockBuffer<T, N> &buffer,
                           const T &data) {
  bind_cpu(3);

  return run_bench(name,
                   [&] {
                     buffer.store(data);
                     auto res = buffer.load();
                     do_not_optimize(res);
                   },
                   {
                       .limit = BenchConfig::DURATION_SEC,
                   });
}

// 示例：多线程竞争场景测试
template <typename T, size_t N>
Stats bench_contention(std::string name, SeqLockBuffer<T, N> &buffer,
                       const T &data) {
  std::atomic<bool> stop{false};

  bind_cpu(3);

  // 生产者线程：在另一个核心上疯狂写入
  std::jthread writer([&](std::stop_token st) {
    bind_cpu(2);
    while (!st.stop_requested()) {
      buffer.store(data);
      std::atomic_signal_fence(std::memory_order_relaxed);
    }
  });

  // 读端测试
  return run_bench(name,
                   [&] {
                     auto res = buffer.load();
                     do_not_optimize(res);
                   },
                   {.limit = BenchConfig::DURATION_SEC * 2});
}

template <size_t Bytes> struct alignas(64) MockData {
  std::array<std::byte, Bytes> payload;
};

int main() {
  std::println("Starting SeqLockBuffer Benchmark...");
  TSC::init();

  using DataSizeList = std::integer_sequence<size_t, 64, 256, 1024, 4096>;
  using BufSizeList = std::integer_sequence<size_t, 8, 64, 256, 1024>;

  std::string title = "snapshot_buffer_store";
  run_benchmark_matrix(title, DataSizeList{}, BufSizeList{},
                       [&]<size_t D, size_t B>() {
                         using DataType = MockData<D>;
                         SeqLockBuffer<DataType, B> buffer;
                         DataType data{};
                         std::string suffix = std::format("_D{}_B{}", D, B);
                         return bench_store(title + suffix, buffer, data);
                       });

  title = "snapshot_buffer_store_load";
  run_benchmark_matrix(
      title, DataSizeList{}, BufSizeList{}, [&]<size_t D, size_t B>() {
        using DataType = MockData<D>;
        SeqLockBuffer<DataType, B> buffer;
        DataType data{};
        std::string suffix = std::format("_D{}_B{}", D, B);
        return bench_store_and_load(title + suffix, buffer, data);
      });

  title = "snapshot_buffer_contention_load";
  run_benchmark_matrix(title, DataSizeList{}, BufSizeList{},
                       [&]<size_t D, size_t B>() {
                         using DataType = MockData<D>;
                         SeqLockBuffer<DataType, B> buffer;
                         DataType data{};
                         std::string suffix = std::format("_D{}_B{}", D, B);
                         return bench_contention(title + suffix, buffer, data);
                       });

  return 0;
}
