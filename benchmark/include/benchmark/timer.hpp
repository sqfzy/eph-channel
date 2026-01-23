#pragma once

#include <chrono>
#include <cstdint>
#include <iostream>
#include <thread>

namespace benchmark {

// TSC 高精度计时器
class TSCClock {
public:
  TSCClock() { calibrate(); }

  static inline uint64_t now() noexcept {
    unsigned int lo, hi;
    __asm__ __volatile__("rdtsc" : "=a"(lo), "=d"(hi));
    return (static_cast<uint64_t>(hi) << 32) | lo;
  }

  double to_ns(uint64_t cycles) const noexcept {
    return cycles * ns_per_cycle_;
  }

  double frequency_ghz() const noexcept { return 1.0 / ns_per_cycle_; }

private:
  double ns_per_cycle_ = 0.0;

  void calibrate() {
    std::cout << "[Timer] Calibrating TSC (100ms)... " << std::flush;

    auto t1 = std::chrono::steady_clock::now();
    uint64_t c1 = now();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto t2 = std::chrono::steady_clock::now();
    uint64_t c2 = now();

    double dt_ns = std::chrono::duration<double, std::nano>(t2 - t1).count();
    ns_per_cycle_ = dt_ns / static_cast<double>(c2 - c1);

    std::cout << "Done. Freq: " << frequency_ghz() << " GHz" << std::endl;
  }
};

} // namespace benchmark