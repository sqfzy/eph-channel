#include "eph/core/queue.hpp"
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

  std::string title = "queue_push";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        auto queue = std::make_unique<BoundedQueue<MockData<D>, B>>();
        MockData<D> data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        return run_bench(title + suffix, [&] { queue->push(data); },
                         {
                             .limit = 5s,
                         });
      });

  title = "queue_push_pop";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        auto queue = std::make_unique<BoundedQueue<MockData<D>, B>>();
        MockData<D> data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        return run_bench(title + suffix,
                         [&] {
                           queue->push(data);
                           auto res = queue->pop();
                           do_not_optimize(res);
                         },
                         {
                             .limit = 5s,
                         });
      });

  title = "queue_contention_pop";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        auto queue = std::make_unique<BoundedQueue<MockData<D>, B>>();
        MockData<D> data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        std::jthread writer([&](std::stop_token st) {
          bind_cpu(3);
          while (!st.stop_requested()) {
            queue->push(data);
            clobber_memory();
          }
        });

        return run_bench(title + suffix,
                         [&] {
                           auto res = queue->pop();
                           do_not_optimize(res);
                         },
                         {.limit = 10s});
      });

  return 0;
}
