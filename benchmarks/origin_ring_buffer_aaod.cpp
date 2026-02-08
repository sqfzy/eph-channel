#include <cstdint>
#include <cstring>
#include <thread>

#include "common.hpp"
#include "origin_ring_buffer.hpp"

int main() {
  eph::bind_cpu(4);
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

        auto buffer = std::make_unique<RingBuffer<B, (uint32_t)sizeof(Data)>>();
        std::string suffix = std::format("_D{}_B{}", D, B);

        std::jthread writer([&](std::stop_token st) {
          eph::bind_cpu(5);
          uint64_t sum_tsc = 0;
          uint64_t count = 0;
          while (!st.stop_requested()) {
            Data data{};
            data.tsc = TSC::now();
            data.sum_tsc = sum_tsc;
            data.count = count;
            buffer->push((uint8_t *)&data, sizeof(data));
            sum_tsc += data.tsc;
            count += 1;
          }
        });

        uint64_t last_sum_tsc = 0;
        uint64_t last_count = 0;

        return run_bench(
            title + suffix,
            [&]() -> std::optional<double> {
              const uint8_t *out_raw = nullptr;
              uint32_t out_len = 0;
              if (!buffer->pop_latest(out_raw, out_len)) {
                return std::nullopt;
              }

              Data out;
              std::memcpy(&out, out_raw, sizeof(Data));

              uint64_t diff_sum = out.sum_tsc - last_sum_tsc;
              uint64_t diff_count = out.count - last_count;

              if (diff_count == 0)
                return std::nullopt;

              uint64_t now = eph::benchmark::TSC::now();

              uint64_t prod;
              if (__builtin_mul_overflow(diff_count, now, &prod)) {
                std::print(
                    "Detected Overflow: count={} * now={} exceeds uint64_t\n",
                    diff_count, now);
              }

              double aaod = static_cast<double>(diff_count * now - diff_sum) /
                            static_cast<double>(diff_count);

              last_sum_tsc = out.sum_tsc;
              last_count = out.count;

              return aaod;
            },
            {.limit = 10s});
      });

  return 0;
}
