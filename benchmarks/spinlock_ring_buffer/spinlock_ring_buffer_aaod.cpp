#include "../common.hpp"
#include "eph/benchmark/recorder.hpp"
#include "eph/benchmark/timer.hpp"
#include "spinlock_ring_buffer.hpp"
#include "eph/platform.hpp"
#include <cstdint>
#include <format>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

int main() {
  bind_cpu(2);
  TSC::init();

  std::string title = "ring_buffer_aaod";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        struct Data {
          uint64_t tsc;
          uint64_t sum_tsc;
          uint64_t count;
          std::array<std::byte, (D > 24 ? D - 24 : 0)> payload;
        };

        std::unique_ptr<RingBuffer<Data, B>> buffer =
            std::make_unique<RingBuffer<Data, B>>();
        Data data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        std::jthread writer([&](std::stop_token st) {
          bind_cpu(3);

          uint64_t sum_tsc = 0;
          uint64_t count = 0;

          while (!st.stop_requested()) {
            data.tsc = TSC::now();
            data.sum_tsc = sum_tsc;
            data.count = count;

            buffer->push(data);

            sum_tsc += data.tsc;
            count += 1;
          }
        });

        uint64_t last_sum_tsc = 0;
        uint64_t last_count = 0;

        return run_bench(title + suffix,
                         [&]() -> std::optional<double> {
                           Data out = buffer->pop();
                           uint64_t diff_sum = out.sum_tsc - last_sum_tsc;
                           uint64_t diff_count = out.count - last_count;

                           if (diff_count == 0) {
                             // 没有新数据，跳过计算
                             return std::nullopt;
                           }

                           uint64_t now = eph::benchmark::TSC::now();
                           double aaod =
                               static_cast<double>(diff_count * now -
                                                   diff_sum) /
                               static_cast<double>(diff_count);

                           last_sum_tsc = out.sum_tsc;
                           last_count = out.count;

                           return aaod;
                         },
                         {.limit = 10s});
      });

  return 0;
}
