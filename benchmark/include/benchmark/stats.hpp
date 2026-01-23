#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <format>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace benchmark {

struct Sample {
  uint64_t seq;
  double latency_ns;
};

class StatsRecorder {
public:
  void add(uint64_t seq, double latency_ns) {
    samples_.push_back({seq, latency_ns});
  }

  void reserve(size_t count) { samples_.reserve(count); }

  void report(const std::string &title) {
    if (samples_.empty()) {
      std::cout << "[Stats] No samples to report." << std::endl;
      return;
    }

    // 先导出 CSV
    export_csv(title + ".csv");

    // 排序
    std::ranges::sort(samples_, [](const auto &a, const auto &b) {
      return a.latency_ns < b.latency_ns;
    });

    size_t n = samples_.size();
    size_t n_p99 = static_cast<size_t>(n * 0.99);

    double sum_all = 0.0;
    double sum_p99 = 0.0;

    for (size_t i = 0; i < n; ++i) {
      sum_all += samples_[i].latency_ns;
      if (i < n_p99) {
        sum_p99 += samples_[i].latency_ns;
      }
    }

    double avg_v = sum_all / n;
    double p99_avg_v = sum_p99 / n_p99;
    double min_v = samples_.front().latency_ns;
    double p50_v = samples_[static_cast<size_t>(n * 0.50)].latency_ns;
    double p99_v = samples_[static_cast<size_t>(n * 0.99)].latency_ns;
    double p999_v = samples_[static_cast<size_t>(n * 0.999)].latency_ns;
    double max_v = samples_.back().latency_ns;


    // 导出 JSON
    export_json(title, min_v, avg_v, p99_avg_v, p50_v, p99_v, p999_v, max_v);

    // 控制台输出
    print_summary(title, n, min_v, avg_v, p99_avg_v, p50_v, p99_v, p999_v,
                  max_v);
  }

private:
  std::vector<Sample> samples_;

  void export_csv(const std::string &filename) {
    std::ofstream file(filename);
    file << "seq,latency_ns\n";
    for (const auto &s : samples_) {
      file << s.seq << "," << s.latency_ns << "\n";
    }
    std::cout << "[Stats] CSV exported to " << filename << std::endl;
  }

  void export_json(const std::string &title, double min_v, double avg_v,
                   double p99_avg_v, double p50_v, double p99_v, double p999_v,
                   double max_v) {
    auto now = std::chrono::system_clock::now();
    auto now_seconds = std::chrono::floor<std::chrono::seconds>(now);
    std::string date_str = std::format("{:%Y-%m-%d_%H%M%S}", now_seconds);
    std::string filename = title + "_report_" + date_str + ".json";

    std::ofstream json(filename);
    json << "{\n"
         << "  \"title\": \"" << title << "\",\n"
         << "  \"date\": \"" << date_str << "\",\n"
         << "  \"sample_count\": " << samples_.size() << ",\n"
         << "  \"stats\": {\n"
         << "    \"min_ns\": " << min_v << ",\n"
         << "    \"avg_ns\": " << avg_v << ",\n"
         << "    \"p99_avg_ns\": " << p99_avg_v << ",\n"
         << "    \"p50_ns\": " << p50_v << ",\n"
         << "    \"p99_ns\": " << p99_v << ",\n"
         << "    \"p999_ns\": " << p999_v << ",\n"
         << "    \"max_ns\": " << max_v << "\n"
         << "  }\n"
         << "}" << std::endl;

    std::cout << "[Stats] JSON exported to " << filename << std::endl;
  }

  void print_summary(const std::string &title, size_t n, double min_v,
                     double avg_v, double p99_avg_v, double p50_v, double p99_v,
                     double p999_v, double max_v) {
    std::cout << "\n=== " << title << " (" << n << " samples) ===\n"
              << "Min:   " << min_v << " ns\n"
              << "Avg:   " << avg_v << " ns\n"
              << "P99 Avg:  " << p99_avg_v << " ns\n"
              << "P50:   " << p50_v << " ns\n"
              << "P99:   " << p99_v << " ns\n"
              << "P99.9: " << p999_v << " ns\n"
              << "Max:   " << max_v << " ns\n"
              << std::endl;
  }
};

} // namespace benchmark
