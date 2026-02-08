#pragma once

#include "timer.hpp"
#include <algorithm>
#include <atomic>
#include <bit>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <print>
#include <string>
#include <sys/resource.h>
#include <variant>
#include <vector>

namespace fs = std::filesystem;

namespace eph::benchmark {

// =========================================================
// 轻量级 HdrHistogram 实现 (Header-only)
// =========================================================
// 精度：3 位有效数字 (sub_bucket_count = 2048)
// 范围：1 到 2^63
class SimpleHdrHistogram {
public:
  SimpleHdrHistogram() {
    // 预分配内存，确保运行时无分配
    // 64 levels * 2048 buckets * 4 bytes ≈ 512 KB
    counts_.resize(kBucketSize * 64, 0);
  }

  void record(uint64_t value) {
    if (value == 0)
      return; // 忽略 0 延迟
    size_t idx = get_index(value);
    if (idx < counts_.size()) {
      counts_[idx]++;
    }
    total_count_++;
  }

  void reset() {
    std::fill(counts_.begin(), counts_.end(), 0);
    total_count_ = 0;
  }

  [[nodiscard]] uint64_t get_value_at_percentile(double percentile) const {
    if (total_count_ == 0)
      return 0;

    double target_count = total_count_ * (percentile / 100.0);
    uint64_t current_count = 0;

    for (size_t i = 0; i < counts_.size(); ++i) {
      if (counts_[i] > 0) {
        current_count += counts_[i];
        if (current_count >= target_count) {
          return get_value_from_index(i);
        }
      }
    }
    return get_max_value_recorded();
  }

  [[nodiscard]] uint64_t get_max_value_recorded() const {
    for (size_t i = counts_.size() - 1; i > 0; --i) {
      if (counts_[i] > 0)
        return get_value_from_index(i);
    }
    return 0;
  }

  // 迭代器接口，用于导出数据
  template <typename Func> void for_each_recorded_value(Func func) const {
    for (size_t i = 0; i < counts_.size(); ++i) {
      if (counts_[i] > 0) {
        func(get_value_from_index(i), counts_[i]);
      }
    }
  }

private:
  static constexpr int kSubBucketBits = 11; // 2^11 = 2048
  static constexpr int kBucketSize = 1 << kSubBucketBits;
  static constexpr uint64_t kSubBucketMask = kBucketSize - 1;

  std::vector<uint32_t> counts_;
  uint64_t total_count_ = 0;

  // 将数值映射到数组索引
  [[nodiscard]] static size_t get_index(uint64_t value) {
    if (value < kBucketSize) {
      return value;
    }
    // 找到最高有效位 (Magnitude)
    int magnitude = std::bit_width(value) - 1;
    // 计算该量级内的偏移
    int shift = magnitude - kSubBucketBits;
    // 索引 = (量级偏移) + (子桶偏移)
    // 量级偏移需要减去基础的 sub_bucket_bits，因为前 kBucketSize 个数直接存
    size_t magnitude_base = (magnitude - kSubBucketBits + 1) << kSubBucketBits;
    size_t sub_bucket = (value >> shift) & kSubBucketMask;

    return magnitude_base + sub_bucket;
  }

  // 将数组索引还原为数值（近似值）
  [[nodiscard]] static uint64_t get_value_from_index(size_t index) {
    if (index < kBucketSize) {
      return index;
    }

    size_t magnitude_idx = index >> kSubBucketBits;
    size_t sub_bucket = index & kSubBucketMask;

    int magnitude = magnitude_idx + kSubBucketBits - 1;
    int shift = magnitude - kSubBucketBits;

    uint64_t value =
        (static_cast<uint64_t>(1) << magnitude) + (sub_bucket << shift);
    return value;
  }
};

struct Stats {
  std::string name;
  uint64_t count;
  double avg_ns;
  double min_ns;
  double max_ns;
  double p50_ns;
  double p99_ns;

  long majflt;       // Major Page Faults
  long minflt;       // Minor Page Faults
  long nvcsw;        // Voluntary Context Switches
  long nivcsw;       // Involuntary Context Switches
  double user_cpu_s; // User CPU time (seconds)
  double sys_cpu_s;  // System CPU time (seconds)
};

class Recorder {
public:
  explicit Recorder(std::string name) : name_(std::move(name)) {
    // Histogram 已经在构造函数中分配好内存
  }

  // =========================================================
  // 记录数据
  // =========================================================
  // 输入单位：Cycles
  void record(double cycles) {
    count_++;
    total_cycles_ += cycles;

    if (cycles < min_cycles_)
      min_cycles_ = cycles;
    if (cycles > max_cycles_)
      max_cycles_ = cycles;

    // HdrHistogram 记录 (转换为整数 Cycles)
    histogram_.record(static_cast<uint64_t>(cycles));
  }

  void set_resource_usage(const rusage &start, const rusage &end) {
    res_majflt_ = end.ru_majflt - start.ru_majflt;
    res_minflt_ = end.ru_minflt - start.ru_minflt;
    res_nvcsw_ = end.ru_nvcsw - start.ru_nvcsw;
    res_nivcsw_ = end.ru_nivcsw - start.ru_nivcsw;

    // 计算 CPU 时间差值
    auto time_diff = [](timeval t1, timeval t2) {
      return (t2.tv_sec - t1.tv_sec) + (t2.tv_usec - t1.tv_usec) / 1e6;
    };
    res_utime_s_ = time_diff(start.ru_utime, end.ru_utime);
    res_stime_s_ = time_diff(start.ru_stime, end.ru_stime);
  }

  // =========================================================
  // 报告统计数据：控制台打印
  // =========================================================
  void print_report() const {
    if (count_ == 0) {
      std::println("[{}] No data recorded.", name_);
      return;
    }

    std::string time_str = get_current_time_str();
    std::string title = std::format(" BENCHMARK REPORT ({}) ", time_str);
    Stats stats = compute_stats();

    // 定义列宽
    constexpr int w_name = 30;
    constexpr int w_metric = 12;
    constexpr int total_w = w_name + (w_metric * 6) + 18;

    std::println("\n{:-^{}}", title, total_w);

    // --- Section 1: Latency (时间延迟) ---
    std::println("{:<{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}}",
                 "Task Name", w_name, "Count", w_metric, "Avg(ns)", w_metric,
                 "P50(ns)", w_metric, "P99(ns)", w_metric, "Min(ns)", w_metric,
                 "Max(ns)", w_metric);

    std::println("{:-^{}}", "", total_w);

    std::println("{:<{}} | {:>{}} | {:>{}.1f} | {:>{}.1f} | {:>{}.1f} | "
                 "{:>{}.1f} | {:>{}.1f}",
                 stats.name, w_name, stats.count, w_metric, stats.avg_ns,
                 w_metric, stats.p50_ns, w_metric, stats.p99_ns, w_metric,
                 stats.min_ns, w_metric, stats.max_ns, w_metric);

    // --- Section 2: System Resources (系统资源) ---
    std::println("{:-^{}}", " System Resources ", total_w);

    // 定义资源部分的列名
    std::println("{:<{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}}",
                 "CPU Time", w_name, "User(s)", w_metric, "Sys(s)", w_metric,
                 "MajFault", w_metric, "MinFault", w_metric, "VolCtx", w_metric,
                 "InvCtx", w_metric);

    std::println("{:-^{}}", "", total_w);

    std::println("{:<{}} | {:>{}.4f} | {:>{}.4f} | {:>{}} | {:>{}} | {:>{}} | "
                 "{:>{}}",
                 "Usage", w_name, stats.user_cpu_s, w_metric, stats.sys_cpu_s,
                 w_metric, stats.majflt, w_metric, stats.minflt, w_metric,
                 stats.nvcsw, w_metric, stats.nivcsw, w_metric);

    std::println("{:-^{}}\n", "", total_w);
  }

  // =========================================================
  // 导出统计数据：JSON
  // =========================================================
  void export_json(const std::string &output_dir = "outputs") const {
    ensure_directory(output_dir);
    Stats stats = compute_stats();
    std::string time_str = get_current_time_str();

    std::string filename = sanitize_filename(name_) + "_" + time_str + ".json";
    fs::path path = fs::path(output_dir) / filename;

    std::ofstream file(path);
    if (!file.is_open())
      return;

    file << std::format(R"({{
  "name": "{}",
  "report_time": "{}",
  "count": {},
  "stats": {{
    "avg_ns": {:.2f},
    "min_ns": {:.2f},
    "max_ns": {:.2f},
    "p50_ns": {:.2f},
    "p99_ns": {:.2f}
  }},
  "resources": {{
    "major_page_faults": {},
    "minor_page_faults": {},
    "voluntary_context_switches": {},
    "involuntary_context_switches": {},
    "user_cpu_seconds": {:.4f},
    "system_cpu_seconds": {:.4f}
  }}
}})",
                        stats.name, time_str, stats.count, stats.avg_ns,
                        stats.min_ns, stats.max_ns, stats.p50_ns, stats.p99_ns,
                        // 新增字段
                        stats.majflt, stats.minflt, stats.nvcsw, stats.nivcsw,
                        stats.user_cpu_s, stats.sys_cpu_s);

    std::println("Stats JSON exported to: {}", path.string());
  }

  // =========================================================
  // 导出分布：CSV (导出直方图 buckets)
  // =========================================================
  // 导出 "Value(ns), Count" 的分布数据。
  void export_samples_to_csv(const std::string &output_dir = "outputs") const {
    ensure_directory(output_dir);
    double ns_per_cycle = TSC::to_ns(1);

    std::string time_str = get_current_time_str();
    fs::path path = fs::path(output_dir) /
                    (sanitize_filename(name_) + "_" + time_str + ".csv");
    std::ofstream file(path);
    if (!file.is_open())
      return;

    file << "value_ns,count\n";

    // 遍历直方图并导出
    histogram_.for_each_recorded_value([&](uint64_t cycles, uint32_t count) {
      file << std::format("{:.2f},{}\n", cycles * ns_per_cycle, count);
    });

    std::println("Distribution CSV exported to: {}", path.string());
  }

  // 计算统计值并转为纳秒
  Stats compute_stats() const {
    if (count_ == 0)
      return {name_, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0, 0, 0, 0.0, 0.0};

    double ns_per_cycle = TSC::to_ns(1);
    double avg_cyc = total_cycles_ / count_;
    double p50_cyc =
        static_cast<double>(histogram_.get_value_at_percentile(50.0));
    double p99_cyc =
        static_cast<double>(histogram_.get_value_at_percentile(99.0));

    return Stats{name_, count_, avg_cyc * ns_per_cycle,
                 min_cycles_ * ns_per_cycle, max_cycles_ * ns_per_cycle,
                 p50_cyc * ns_per_cycle, p99_cyc * ns_per_cycle,
                 // 传递资源数据
                 res_majflt_, res_minflt_, res_nvcsw_, res_nivcsw_,
                 res_utime_s_, res_stime_s_};
  }

  constexpr uint64_t count() const { return count_; }

  // 重置记录器 (清空 Histogram)
  void reset() {
    count_ = 0;
    total_cycles_ = 0.0;
    min_cycles_ = std::numeric_limits<double>::max();
    max_cycles_ = 0.0;
    histogram_.reset();
  }

private:
  std::string name_;
  uint64_t count_ = 0;
  double total_cycles_ = 0.0;
  double min_cycles_ = std::numeric_limits<double>::max();
  double max_cycles_ = 0.0;

  long res_majflt_ = 0;
  long res_minflt_ = 0;
  long res_nvcsw_ = 0;
  long res_nivcsw_ = 0;
  double res_utime_s_ = 0.0;
  double res_stime_s_ = 0.0;

  SimpleHdrHistogram histogram_;

  // 获取格式化时间串：YYYY-MM-DD-HH:MM:SS
  std::string get_current_time_str() const {
    auto now = std::chrono::system_clock::now();
    auto now_sec = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%Y-%m-%d-%H:%M:%S}", now_sec);
  }

  void ensure_directory(const std::string &path) const {
    if (!fs::exists(path)) {
      fs::create_directories(path);
    }
  }

  std::string sanitize_filename(std::string name) const {
    std::replace_if(
        name.begin(), name.end(),
        [](char c) {
          return c == '/' || c == '\\' || c == ':' || c == ' ' || c == '<' ||
                 c == '>';
        },
        '_');
    return name;
  }
};

// =========================================================
// 自动化基准测试入口
// =========================================================

struct BenchOptions {
  // 运行模式：次数 或 持续时间
  std::variant<size_t, std::chrono::seconds> limit = size_t(10000);
  // 预热次数 (不计入统计)
  size_t warmup = 100;
  // 导出文件的目录
  std::string output_dir = "outputs";
  bool export_json = false;
  bool export_csv = false;
};

/**
 * @brief 自动运行基准测试、打印报告并导出 JSON/CSV。
 * @tparam Func 待测函数类型 (通常是 Lambda)
 * @param name 测试任务名称
 * @param func 待测代码块
 * @param options 配置项 (可选, 支持指定初始化)
 */
template <typename Func>
  requires std::invocable<Func>
Stats run_bench(std::string name, Func &&func, BenchOptions options = {}) {
  // 1. 准备 Recorder
  Recorder recorder(std::move(name));

  // 定义单次运行逻辑
  auto bench_once = [&]() {
    using Ret = std::invoke_result_t<Func>;
    if constexpr (std::is_same_v<Ret, double>) {
      // 如果函数返回 double，直接记录其返回值
      recorder.record(std::invoke(func));
    } else if constexpr (std::is_same_v<Ret, std::optional<double>>) {
      // 如果返回 optional，仅在有值时记录
      if (auto res = std::invoke(func); res.has_value()) {
        recorder.record(*res);
      }
    } else {
      // 否则使用 measure 包装器测量执行时间
      recorder.record(static_cast<double>(measure(func)));
    }
  };

  // 2. 预热阶段 (Warmup)
  // 目的: 填充指令缓存 (I-Cache)、激活分支预测器、甚至触发 JIT/PageFault
  for (size_t i = 0; i < options.warmup; ++i) {
    // 简单调用，不进行计时
    std::invoke(func);
    // 强行屏障，防止编译器将预热循环优化掉
    std::atomic_signal_fence(std::memory_order_relaxed);
  }

  struct rusage start_ru, end_ru;
  getrusage(RUSAGE_SELF, &start_ru);

  // 3. 正式测量阶段 (Benchmark)
  if (std::holds_alternative<size_t>(options.limit)) {
    // 基于次数模式
    size_t limit = std::get<size_t>(options.limit);
    while (recorder.count() < limit) {
      bench_once();
    }
  } else {
    // 基于时间模式
    auto duration = std::get<std::chrono::seconds>(options.limit);
    auto start_time = std::chrono::steady_clock::now();

    while (std::chrono::steady_clock::now() - start_time < duration) {
      bench_once();
    }
  }

  getrusage(RUSAGE_SELF, &end_ru);
  recorder.set_resource_usage(start_ru, end_ru);

  // 4. 导出
  // 控制台报告
  recorder.print_report();
  // 详细统计 JSON
  if (options.export_json) {
    recorder.export_json(options.output_dir);
  }
  // 原始分布 CSV
  if (options.export_csv) {
    recorder.export_samples_to_csv(options.output_dir);
  }

  return recorder.compute_stats();
}

} // namespace eph::benchmark
