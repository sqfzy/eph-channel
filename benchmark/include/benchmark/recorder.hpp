#pragma once

#include "timer.hpp"
#include <algorithm>
#include <filesystem>
#include <format>
#include <fstream>
#include <limits>
#include <print>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using namespace benchmark;

class Recorder {
public:
  explicit Recorder(std::string name) : name_(std::move(name)) {
    samples_.reserve(4096);
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

    samples_.push_back(cycles);
  }

  // =========================================================
  // 报告统计数据：控制台打印
  // =========================================================
  void print_report() const {
    if (samples_.empty()) {
      std::println("[{}] No data recorded.", name_);
      return;
    }

    std::string time_str = get_current_time_str();
    std::string title = std::format(" BENCHMARK REPORT ({}) ", time_str);
    Stats stats = compute_stats_ns();

    constexpr int w_name = 40;
    constexpr int w_count = 10;
    constexpr int w_data = 12;

    constexpr int total_w = w_name + w_count + (w_data * 5) + 18;

    std::println("\n{:-^{}}", title, total_w);
    std::println("{:<{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}} | {:>{}}",
                 "Name", w_name, "Count", w_count, "Avg(ns)", w_data, "Min(ns)",
                 w_data, "P50(ns)", w_data, "P99(ns)", w_data, "Max(ns)",
                 w_data);

    std::println("{:-^{}}", "", total_w);

    std::println("{:<{}} | {:>{}} | {:>{}.2f} | {:>{}.2f} | {:>{}.2f} | "
                 "{:>{}.2f} | {:>{}.2f}",
                 stats.name, w_name, stats.count, w_count, stats.avg_ns, w_data,
                 stats.min_ns, w_data, stats.p50_ns, w_data, stats.p99_ns,
                 w_data, stats.max_ns, w_data);

    std::println("{:-^{}}\n", "", total_w);
  }

  // =========================================================
  // 导出统计数据：JSON
  // =========================================================
  void export_json(const std::string &output_dir = "outputs") const {
    ensure_directory(output_dir);
    Stats stats = compute_stats_ns();
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
  }}
}})",
                        stats.name, time_str, stats.count, stats.avg_ns,
                        stats.min_ns, stats.max_ns, stats.p50_ns, stats.p99_ns);

    std::println("Stats JSON exported to: {}", path.string());
  }

  // =========================================================
  // 导出样本：CSV
  // =========================================================
  void export_samples_to_csv(const std::string &output_dir = "outputs") const {
    ensure_directory(output_dir);
    double ns_per_cycle = TSC::get().to_ns(1);

    std::string time_str = get_current_time_str();
    fs::path path = fs::path(output_dir) /
                    (sanitize_filename(name_) + "_" + time_str + ".csv");
    std::ofstream file(path);
    if (!file.is_open())
      return;

    file << "seq,latency_ns\n";
    for (size_t i = 0; i < samples_.size(); ++i) {
      file << std::format("{},{:.2f}\n", i + 1, samples_[i] * ns_per_cycle);
    }

    std::println("Raw samples CSV exported to: {}", path.string());
  }

private:
  struct Stats {
    std::string name;
    uint64_t count;
    double avg_ns;
    double min_ns;
    double max_ns;
    double p50_ns;
    double p99_ns;
  };

  std::string name_;
  uint64_t count_ = 0;
  // 存储原始 Cycles
  double total_cycles_ = 0.0;
  double min_cycles_ = std::numeric_limits<double>::max();
  double max_cycles_ = 0.0;
  std::vector<double> samples_;

  // 获取格式化时间串：YYYY-MM-DD-HH:MM:SS
  std::string get_current_time_str() const {
    auto now = std::chrono::system_clock::now();
    auto now_sec = std::chrono::floor<std::chrono::seconds>(now);
    return std::format("{:%Y-%m-%d-%H:%M:%S}", now_sec);
  }

  // 内部辅助：计算统计值并转为纳秒
  Stats compute_stats_ns() const {
    if (samples_.empty()) {
      return {name_, 0, 0.0, 0.0, 0.0, 0.0, 0.0};
    }

    // 1. 获取当前转换倍率 (1 Cycle = ? ns)
    // 技巧：调用 to_ns(1) 获取 ns_per_cycle
    double ns_per_cycle = TSC::get().to_ns(1);

    // 2. 拷贝并排序样本 (Cycles)
    std::vector<double> sorted = samples_;
    std::sort(sorted.begin(), sorted.end());

    // 3. 计算 Cycles 统计值
    double avg_cyc = total_cycles_ / count_;
    double p50_cyc = sorted[sorted.size() / 2];
    size_t p99_idx = static_cast<size_t>(sorted.size() * 0.99);
    double p99_cyc = sorted[std::min(p99_idx, sorted.size() - 1)];

    // 4. 统一转换为 ns
    return Stats{name_,
                 count_,
                 avg_cyc * ns_per_cycle,
                 min_cycles_ * ns_per_cycle,
                 max_cycles_ * ns_per_cycle,
                 p50_cyc * ns_per_cycle,
                 p99_cyc * ns_per_cycle};
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
