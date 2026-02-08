#include <cstdint>
#include <cstring>
#include <thread>

#include "common.hpp"
#include "eph/benchmark/timer.hpp"
#include "eph/platform.hpp"
#include "origin_ring_buffer.hpp"

int main() {
  eph::bind_cpu(2);
  TSC::init();

  std::string title = "ring_buffer_contention";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        struct Data {
          uint64_t head_canary; // 头部哨兵
          std::array<std::byte, (D > 16 ? D - 16 : 0)> payload;
          uint64_t tail_canary; // 尾部哨兵
        };

        auto buffer = std::make_unique<RingBuffer<B, (uint32_t)sizeof(Data)>>();
        Data data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        std::jthread writer([&](std::stop_token st) {
          eph::bind_cpu(3);

          uint64_t duration = TSC::to_cycles(1s);
          uint64_t seq = 1;
          while (!st.stop_requested()) {
            data.head_canary = seq;
            data.tail_canary = seq; // 保证写入时 head == tail
            buffer->push(reinterpret_cast<const uint8_t *>(&data),
                         sizeof(data));

            // FIX: 避免回绕后发生竞争
            double end = TSC::now() + duration;
            while (TSC::now() < end) {
              eph::cpu_relax();
            }
            seq++;
          }
        });

        return run_bench(title + suffix,
                         [&] {
                           const uint8_t *out_raw = nullptr;
                           uint32_t out_len = 0;

                           if (buffer->pop_latest(out_raw, out_len)) {
                             // 注意：此时 origin_ring_buffer 已经执行了
                             // tail.store 生产者随时可以修改 out_raw 指向的内存

                             Data local;
                             // 执行读取
                             std::memcpy(&local, out_raw, sizeof(Data));

                             // 核心验证：如果 head_canary != tail_canary，证明 memcpy
                             // 期间内存被 Writer 修改了
                             if (local.head_canary != local.tail_canary) {
                               std::print(
                                   stderr,
                                   "\n[MATCH] 检测到瞬时并发读写 (Data Race)!\n"
                                   "读取到的数据发生撕裂：\n"
                                   "  Head Canary: {}\n"
                                   "  Tail Canary: {}\n"
                                   "这证明在 Reader 执行 memcpy 时，Writer "
                                   "正在并发写入同一块内存。\n",
                                   local.head_canary, local.tail_canary);
                               std::abort();
                             }
                           }
                         },
                         {.limit = 10s});
      });

  return 0;
}
