#include <cstdint>
#include <cstring>

#include "origin_ring_buffer.hpp"
#include "common.hpp"

int main() {
  eph::bind_cpu(2);
  TSC::init();

  std::string title = "ring_buffer_push";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        // 第一个参数是容量(uint32_t)，第二个是槽位大小(uint32_t)
        // 业务逻辑不变：槽位大小设为 sizeof(MockData<D>)
        auto buffer =
            std::make_unique<RingBuffer<B, (uint32_t)sizeof(MockData<D>)>>();
        MockData<D> data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        return run_bench(title + suffix,
                         [&] {
                           buffer->push(
                               reinterpret_cast<const uint8_t *>(&data),
                               sizeof(data));
                         },
                         {.limit = 5s});
      });

  title = "ring_buffer_push_pop";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        auto buffer =
            std::make_unique<RingBuffer<B, (uint32_t)sizeof(MockData<D>)>>();
        MockData<D> data{};
        const uint8_t *out_ptr = nullptr;
        uint32_t out_len = 0;
        std::string suffix = std::format("_D{}_B{}", D, B);

        return run_bench(title + suffix,
                         [&] {
                           const uint8_t *out_ptr = nullptr;
                           uint32_t out_len = 0;
                           buffer->push(
                               reinterpret_cast<const uint8_t *>(&data),
                               sizeof(data));
                           bool res = buffer->pop_latest(out_ptr, out_len);
                           do_not_optimize(res);
                           do_not_optimize(out_ptr);
                         },
                         {.limit = 5s});
      });

  return 0;
}
