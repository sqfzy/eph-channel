#pragma once

#include <cstddef>

namespace shm {

// 配置常量
namespace config {
constexpr size_t DEFAULT_CAPACITY = 1024;
constexpr size_t CACHE_LINE_SIZE = 64;
} // namespace config

// 确保容量是 2 的幂
constexpr bool is_power_of_two(size_t n) { return n > 0 && (n & (n - 1)) == 0; }

} // namespace shm
