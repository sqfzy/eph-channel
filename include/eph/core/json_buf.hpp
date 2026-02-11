#include "eph/platform.hpp"
#include <cstdint>
#include <cstring>
#include <string_view>

namespace eph {

// 保持 64 字节对齐以匹配 Cache Line
alignas(64) constexpr char DIGITS_LUT[] =
    "0001020304050607080910111213141516171819"
    "2021222324252627282930313233343536373839"
    "4041424344454647484950515253545556575859"
    "6061626364656667686970717273747576777879"
    "8081828384858687888990919293949596979899";

struct alignas(64) JsonBuf {
  static constexpr uint32_t CAPACITY = 2048;
  char data[CAPACITY];
  uint32_t len = 0;

  // 清空缓冲区
  ALWAYS_INLINE void reset() noexcept { len = 0; }

  // 写入字符串字面量 (编译期确定长度)
  template <size_t N>
  ALWAYS_INLINE void append_lit(const char (&s)[N]) noexcept {
    constexpr uint32_t n = N - 1;
    std::memcpy(data + len, s, n);
    len += n;
  }

  // 写入变长 string_view
  ALWAYS_INLINE void append_sv(std::string_view sv) noexcept {
    const uint32_t n = static_cast<uint32_t>(sv.size());
    std::memcpy(data + len, sv.data(), n);
    len += n;
  }

  // 极致并行化的 13 位时间戳写入
  ALWAYS_INLINE void append_ts13(uint64_t v) noexcept {
    char *const curr = data + len;

    uint64_t high = v / 100'000'000;                       // 前 5 位
    uint32_t low = static_cast<uint32_t>(v % 100'000'000); // 后 8 位

    // 处理高 5 位 (例如 17392)
    uint32_t h1 = static_cast<uint32_t>(high / 100);
    uint32_t h2 = static_cast<uint32_t>(high % 100);
    curr[0] = static_cast<char>('0' + (h1 / 100));
    std::memcpy(curr + 1, &DIGITS_LUT[(h1 % 100) << 1], 2);
    std::memcpy(curr + 3, &DIGITS_LUT[h2 << 1], 2);

    // 处理低 8 位 (例如 60000000)
    uint32_t l1 = low / 10'000;
    uint32_t l2 = low % 10'000;
    std::memcpy(curr + 5, &DIGITS_LUT[(l1 / 100) << 1], 2);
    std::memcpy(curr + 7, &DIGITS_LUT[(l1 % 100) << 1], 2);
    std::memcpy(curr + 9, &DIGITS_LUT[(l2 / 100) << 1], 2);
    std::memcpy(curr + 11, &DIGITS_LUT[(l2 % 100) << 1], 2);

    len += 13;
  }

  // 获取当前结果
  [[nodiscard]] inline std::string_view view() const noexcept {
    return {data, len};
  }
};

} // namespace eph
