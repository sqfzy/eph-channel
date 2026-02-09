// #include "common.hpp"
// #include "eph/benchmark/timer.hpp"
// #include <algorithm>
// #include <atomic>
// #include <memory>
// #include <numeric>
// #include <thread>
// #include <vector>
//
// using namespace eph;
// using namespace eph::benchmark;
//
// template <typename T, size_t N> struct RingBufferStable {
//   struct alignas(64) Slot {
//     std::atomic<uint64_t> seq{0};
//     T data_{};
//   };
//
//   alignas(64) std::atomic<uint64_t> global_index_{0};
//   Slot slots_[N];
//
//   void push(const T &val) noexcept {
//     uint64_t idx = global_index_.load(std::memory_order_relaxed);
//     Slot &s = slots_[idx & (N - 1)];
//     uint64_t seq = s.seq.load(std::memory_order_relaxed);
//     s.seq.store(seq + 1, std::memory_order_release);
//     s.data_ = val;
//     s.seq.store(seq + 2, std::memory_order_release);
//     global_index_.store(idx + 1, std::memory_order_release);
//   }
//
//   bool try_read_latest(T &out) const noexcept {
//     uint64_t idx = global_index_.load(std::memory_order_acquire);
//     if (idx == 0)
//       return false;
//     const Slot &s = slots_[(idx - 1) & (N - 1)];
//     uint64_t seq1 = s.seq.load(std::memory_order_acquire);
//     if (seq1 & 1)
//       return false;
//     out = s.data_;
//     std::atomic_thread_fence(std::memory_order_acquire);
//     uint64_t seq2 = s.seq.load(std::memory_order_relaxed);
//     return seq1 == seq2;
//   }
// };
//
// // 按照你提供的结构定义 Data
// struct Data {
//   uint64_t tsc;
//   uint64_t sum_tsc;
//   uint64_t count;
// };
//
// template <size_t N>
// void run_latency_test_row(const char *label) {
//     using RB = RingBufferStable<Data, N>;
//     auto rb = std::make_unique<RB>();
//
//     std::atomic<bool> running{true};
//     std::atomic<bool> start{false};
//
//     std::vector<double> aaod_results;
//     aaod_results.reserve(20'000'000);
//
//     // 新增统计项
//     uint64_t total_attempts = 0;
//     uint64_t successful_reads = 0;
//
//     {
//         std::jthread reader([&](std::stop_token st) {
//             bind_cpu(3);
//             while (!start.load(std::memory_order_relaxed));
//
//             uint64_t last_sum_tsc = 0;
//             uint64_t last_count = 0;
//             Data out;
//
//             while (!st.stop_requested() &&
//             running.load(std::memory_order_relaxed)) {
//                 total_attempts++; // 记录总尝试次数
//
//                 if (rb->try_read_latest(out)) {
//                     successful_reads++; // 记录逻辑成功的读取
//                     uint64_t now = TSC::now();
//                     uint64_t diff_sum = out.sum_tsc - last_sum_tsc;
//                     uint64_t diff_count = out.count - last_count;
//
//                     if (diff_count > 0) {
//                         double aaod = (static_cast<double>(diff_count) * now
//                         -
//                                        static_cast<double>(diff_sum)) /
//                                       static_cast<double>(diff_count);
//                         aaod_results.push_back(aaod);
//                         last_sum_tsc = out.sum_tsc;
//                         last_count = out.count;
//                     }
//                 }
//             }
//         });
//
//         std::jthread writer([&](std::stop_token st) {
//             bind_cpu(2);
//             auto warm_up_until = TSC::now() + TSC::to_cycles(200'000'000);
//             while (TSC::now() < warm_up_until);
//
//             uint64_t sum_tsc = 0;
//             uint64_t count = 0;
//             start.store(true);
//
//             while (!st.stop_requested() &&
//             running.load(std::memory_order_relaxed)) {
//                 uint64_t now = TSC::now();
//                 sum_tsc += now;
//                 count += 1;
//                 rb->push({now, sum_tsc, count});
//             }
//         });
//
//         std::this_thread::sleep_for(std::chrono::seconds(5));
//         running.store(false);
//     }
//
//     if (aaod_results.empty()) {
//         std::print("| {:<10} | {:>45} No Samples {:>45} |\n", label, "", "");
//         return;
//     }
//
//     std::sort(aaod_results.begin(), aaod_results.end());
//     auto p50 = aaod_results[aaod_results.size() * 0.5];
//     auto p99 = aaod_results[aaod_results.size() * 0.99];
//     double avg = std::accumulate(aaod_results.begin(), aaod_results.end(),
//     0.0) / aaod_results.size();
//
//     // 计算重试率
//     double retry_rate = 100.0 * (1.0 - (double)successful_reads /
//     total_attempts);
//
//     // 格式化输出，增加 Retry% 列
//     std::print("| {:<8} | {:>8.1f} | {:>8.1f} | {:>10.1f} | {:>8.1f}% |
//     {:>12} |\n",
//                label,
//                TSC::to_ns(p50),
//                TSC::to_ns(p99),
//                TSC::to_ns(avg),
//                retry_rate,
//                aaod_results.size());
// }
//
// int main() {
//     TSC::init();
//
//     std::print("\nBenchmark: AAOD & Read Contention Test\n");
//     // 调整表头
//     std::print("+{:-<10}+{:-<10}+{:-<10}+{:-<12}+{:-<10}+{:-<14}+\n", "", "",
//     "", "", "", ""); std::print("| {:<8} | {:>8} | {:>8} | {:>10} | {:>8} |
//     {:>12} |\n",
//                "Capacity", "P50 (ns)", "P99 (ns)", "Avg (ns)", "Retry%",
//                "Samples");
//     std::print("+{:-<10}+{:-<10}+{:-<10}+{:-<12}+{:-<10}+{:-<14}+\n", "", "",
//     "", "", "", "");
//
//     run_latency_test_row<1>("N=1");
//     run_latency_test_row<2>("N=2");
//     run_latency_test_row<4>("N=4");
//     run_latency_test_row<8>("N=8");
//     run_latency_test_row<16>("N=16");
//     run_latency_test_row<64>("N=64");
//     run_latency_test_row<256>("N=256");
//     run_latency_test_row<1024>("N=1024");
//
//     std::print("+{:-<10}+{:-<10}+{:-<10}+{:-<12}+{:-<10}+{:-<14}+\n\n", "",
//     "", "", "", "", "");
//
//     return 0;
// }
//
#include "common.hpp"
#include "eph/benchmark/timer.hpp"
#include <algorithm>
#include <atomic>
#include <memory>
#include <numeric>
#include <thread>
#include <vector>

enum class ReadStatus {
  Success,
  Busy,    // seq1 & 1: 写者正在操作
  Overlap, // seq1 != seq2: 读取中途被写者覆盖
  Empty    // idx == 0
};

template <typename T, size_t N> struct RingBufferStable {
  struct alignas(64) Slot {
    std::atomic<uint64_t> seq{0};
    T data_{};
  };

  alignas(64) std::atomic<uint64_t> global_index_{0};
  Slot slots_[N];

  // ... push 逻辑保持不变 ...
  void push(const T &val) noexcept {
    uint64_t idx = global_index_.load(std::memory_order_relaxed);
    Slot &s = slots_[idx & (N - 1)];
    uint64_t seq = s.seq.load(std::memory_order_relaxed);
    s.seq.store(seq + 1, std::memory_order_release);
    s.data_ = val;
    s.seq.store(seq + 2, std::memory_order_release);
    global_index_.store(idx + 1, std::memory_order_release);
  }

  ReadStatus try_read_detailed(T &out) const noexcept {
    uint64_t idx = global_index_.load(std::memory_order_acquire);
    if (idx == 0)
      return ReadStatus::Empty;

    const Slot &s = slots_[(idx - 1) & (N - 1)];
    uint64_t seq1 = s.seq.load(std::memory_order_acquire);

    if (seq1 & 1)
      return ReadStatus::Busy;

    out = s.data_;
    std::atomic_thread_fence(std::memory_order_acquire);

    uint64_t seq2 = s.seq.load(std::memory_order_relaxed);
    return (seq1 == seq2) ? ReadStatus::Success : ReadStatus::Overlap;
  }
};

struct Data {
  uint64_t tsc;
  uint64_t sum_tsc;
  uint64_t count;
};

template <size_t N> void run_latency_test_row(const char *label) {
  using RB = RingBufferStable<Data, N>;
  auto rb = std::make_unique<RB>();

  std::atomic<bool> running{true};
  std::atomic<bool> start{false};
  std::vector<double> aaod_results;
  aaod_results.reserve(20'000'000);

  // 细化统计计数器
  uint64_t attempts = 0;
  uint64_t busy_count = 0;
  uint64_t overlap_count = 0;

  {
    std::jthread reader([&](std::stop_token st) {
      eph::bind_cpu(3);
      while (!start.load(std::memory_order_relaxed))
        ;

      uint64_t last_sum_tsc = 0;
      uint64_t last_count = 0;
      Data out;

      while (!st.stop_requested() && running.load(std::memory_order_relaxed)) {
        attempts++;
        auto status = rb->try_read_detailed(out);

        if (status == ReadStatus::Success) {
          uint64_t now = TSC::now();
          uint64_t diff_sum = out.sum_tsc - last_sum_tsc;
          uint64_t diff_count = out.count - last_count;

          if (diff_count > 0) {
            double aaod = (static_cast<double>(diff_count) * now -
                           static_cast<double>(diff_sum)) /
                          static_cast<double>(diff_count);
            aaod_results.push_back(aaod);
            last_sum_tsc = out.sum_tsc;
            last_count = out.count;
          }
        } else if (status == ReadStatus::Busy) {
          busy_count++;
        } else if (status == ReadStatus::Overlap) {
          overlap_count++;
        }
      }
    });

    std::jthread writer([&](std::stop_token st) {
      eph::bind_cpu(2);
      auto warm_up_until = TSC::now() + TSC::to_cycles(200'000'000);
      while (TSC::now() < warm_up_until)
        ;

      uint64_t sum_tsc = 0;
      uint64_t count = 0;
      start.store(true);

      while (!st.stop_requested() && running.load(std::memory_order_relaxed)) {
        uint64_t now = TSC::now();
        sum_tsc += now;
        count += 1;
        rb->push({now, sum_tsc, count});
      }
    });

    std::this_thread::sleep_for(std::chrono::seconds(5));
    running.store(false);
  }

  // 统计计算
  std::sort(aaod_results.begin(), aaod_results.end());
  auto p50 = aaod_results.empty() ? 0 : aaod_results[aaod_results.size() * 0.5];
  auto p99 =
      aaod_results.empty() ? 0 : aaod_results[aaod_results.size() * 0.99];
  double avg = aaod_results.empty() ? 0
                                    : std::accumulate(aaod_results.begin(),
                                                      aaod_results.end(), 0.0) /
                                          aaod_results.size();

  double busy_p = 100.0 * busy_count / attempts;
  double overlap_p = 100.0 * overlap_count / attempts;

  std::print(
      "| {:<8} | {:>8.1f} | {:>10.1f} | {:>7.2f}% | {:>7.2f}% | {:>12} |\n",
      label, TSC::to_ns(p50), TSC::to_ns(avg), busy_p, overlap_p,
      aaod_results.size());
}

int main() {
  TSC::init();
  std::print("\nBenchmark: AAOD Conflict Analysis\n");
  std::print("+{:-<10}+{:-<10}+{:-<12}+{:-<10}+{:-<10}+{:-<14}+\n", "", "", "",
             "", "", "");
  std::print("| {:<8} | {:>8} | {:>10} | {:>8} | {:>8} | {:>12} |\n",
             "Capacity", "P50(ns)", "Avg(ns)", "Busy%", "Overlap%", "Samples");
  std::print("+{:-<10}+{:-<10}+{:-<12}+{:-<10}+{:-<10}+{:-<14}+\n", "", "", "",
             "", "", "");

  run_latency_test_row<1>("N=1");
  run_latency_test_row<2>("N=2");
  run_latency_test_row<4>("N=4");
  run_latency_test_row<16>("N=16");
  run_latency_test_row<256>("N=256");
  run_latency_test_row<1024>("N=1024");

  std::print("+{:-<10}+{:-<10}+{:-<12}+{:-<10}+{:-<10}+{:-<14}+\n\n", "", "",
             "", "", "", "");
  return 0;
}
