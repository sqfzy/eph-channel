#include "eph/core/ring_buffer.hpp"
#include "common.hpp"
#include "eph/benchmark/recorder.hpp"
#include "eph/benchmark/timer.hpp"
#include "eph/platform.hpp"
#include <format>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

int main() {
  bind_cpu(2);
  TSC::init();

  std::string title = "ring_buffer_push";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        auto buffer = std::make_unique<RingBuffer<MockData<D>, B>>();
        MockData<D> data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        return run_bench(title + suffix, [&] { buffer->push(data); },
                         {
                             .limit = 5s,
                         });
      });

  title = "ring_buffer_push_pop";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        auto buffer = std::make_unique<RingBuffer<MockData<D>, B>>();
        MockData<D> data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        return run_bench(title + suffix,
                         [&] {
                           buffer->push(data);
                           auto res = buffer->pop();
                           do_not_optimize(res);
                         },
                         {
                             .limit = 5s,
                         });
      });

  title = "ring_buffer_contention_pop";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        auto buffer = std::make_unique<RingBuffer<MockData<D>, B>>();
        MockData<D> data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        std::jthread writer([&](std::stop_token st) {
          bind_cpu(3);
          while (!st.stop_requested()) {
            buffer->push(data);
            clobber_memory();
          }
        });

        return run_bench(title + suffix,
                         [&] {
                           auto res = buffer->pop();
                           do_not_optimize(res);
                         },
                         {.limit = 10s});
      });

  return 0;
}
