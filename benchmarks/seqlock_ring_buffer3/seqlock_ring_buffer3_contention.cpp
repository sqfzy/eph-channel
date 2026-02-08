#include "../common.hpp"
#include "eph/benchmark/recorder.hpp"
#include "eph/benchmark/timer.hpp"
#include "seqlock_ring_buffer3.hpp"
#include "eph/platform.hpp"
#include <format>
#include <thread>

using namespace eph;
using namespace eph::benchmark;

// 1. 定义检测结构体
int main() {
  bind_cpu(4);
  TSC::init();

  std::string title = "ring_buffer_contention";
  run_benchmark_matrix(
      title, DataSizeList{}, CapacityList{}, [&]<size_t D, size_t B>() {
        struct Data {
          uint64_t head_canary; // 头部哨兵
          std::array<std::byte, (D > 16 ? D - 16 : 0)> payload;
          uint64_t tail_canary; // 尾部哨兵
        };

        auto buffer = std::make_unique<RingBuffer<Data, B>>();
        Data data{};
        std::string suffix = std::format("_D{}_B{}", D, B);

        std::jthread writer([&](std::stop_token st) {
          bind_cpu(5);

          uint64_t seq = 1;
          while (!st.stop_requested()) {
            data.head_canary = seq;
            data.tail_canary = seq; // 保证写入时 head == tail
            buffer->push(data);
            seq++;
          }
        });

        return run_bench(title + suffix,
                         [&] {
                           Data out = buffer->pop();

                           if (out.head_canary != out.tail_canary) {
                             std::print(
                                 stderr,
                                 "\n[MATCH] 检测到瞬时并发读写 (Data Race)!\n"
                                 "读取到的数据发生撕裂：\n"
                                 "  Head Canary: {}\n"
                                 "  Tail Canary: {}\n"
                                 "这证明在 Reader 执行 memcpy 时，Writer "
                                 "正在并发写入同一块内存。\n",
                                 out.head_canary, out.tail_canary);
                             std::abort();
                           }
                         },
                         {.limit = 10s});
      });

  return 0;
}
