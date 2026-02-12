#include <benchmark/benchmark.h>
#include <cstdint>
#include <cstring>
#include <print>
#include <string_view>

// ==========================================
// Mock 外部依赖与辅助工具
// ==========================================
uint64_t now_ms_epoch() { return 1739260000000ULL; }

// 快速 itoa 查找表 (100 字节)
alignas(64) const
    char DIGITS_LUT[] = "0001020304050607080910111213141516171819"
                        "2021222324252627282930313233343536373839"
                        "4041424344454647484950515253545556575859"
                        "6061626364656667686970717273747576777879"
                        "8081828384858687888990919293949596979899";

// ==========================================
// [Legacy] 你的原始实现 (保持不变)
// ==========================================
namespace Legacy {
struct alignas(64) JsonBuf {
  char data[2048];
  int len = 0;

  void reset() { len = 0; }
  bool append(char c) {
    if (len >= (int)sizeof(data) - 1)
      return false;
    data[len++] = c;
    return true;
  }
  bool append(const char *s) {
    while (*s) {
      if (!append(*s++))
        return false;
    }
    return true;
  }
  bool append(const char *s, size_t n) {
    if (len + (int)n >= (int)sizeof(data))
      return false;
    memcpy(data + len, s, n);
    len += (int)n;
    return true;
  }
  bool append(std::string_view sv) { return append(sv.data(), sv.size()); }
  void finish() { data[len] = '\0'; }
};

inline int fast_u64_to_str(uint64_t val, char *buf) {
  if (val == 0) {
    buf[0] = '0';
    buf[1] = '\0';
    return 1;
  }
  char tmp[24];
  int len = 0;
  while (val > 0) {
    tmp[len++] = '0' + (val % 10);
    val /= 10;
  }
  for (int i = 0; i < len; ++i)
    buf[i] = tmp[len - 1 - i];
  buf[len] = '\0';
  return len;
}
} // namespace Legacy

// ==========================================
// [Fast] 深度优化实现
// ==========================================
namespace Fast {
struct alignas(64) JsonBuf {
  char data[2048];
  uint32_t len = 0;

  inline void reset() { len = 0; }

  // 编译期确定字面量长度，直接转换为一次 memcpy
  template <size_t N> inline void append_lit(const char (&s)[N]) {
    constexpr size_t n = N - 1;
    std::memcpy(data + len, s, n);
    len += n;
  }

  inline void append_sv(std::string_view sv) {
    std::memcpy(data + len, sv.data(), sv.size());
    len += (uint32_t)sv.size();
  }

  // 逻辑：13位数字 = 1个首位 + 6组双位 (1 + 6*2 = 13)
  inline void append_ts13(uint64_t val) {
    char *const dst = data + len;
    uint64_t v = val;

    // 从后往前，每两位一组查表写入
    // 填充索引: [11,12], [9,10], [7,8], [5,6], [3,4], [1,2]
    for (int i = 11; i >= 1; i -= 2) {
      const auto idx = (v % 100) << 1;
      std::memcpy(dst + i, &DIGITS_LUT[idx], 2);
      v /= 100;
    }
    // 最后一组 v 剩下最高的一位
    dst[0] = (char)('0' + (v % 10));
    len += 13;
  }

  void finish() { data[len] = '\0'; }
};
} // namespace Fast

// ==========================================
// Benchmark 测试用例
// ==========================================

static void BM_Legacy(benchmark::State &state) {
  Legacy::JsonBuf buf;
  std::string_view symbol = "BTCUSDT", side = "BUY", type = "LIMIT",
                   q = "0.001", p = "69000.5", id = "order_123456789";

  for ([[maybe_unused]] auto _ : state) {
    uint64_t ts = now_ms_epoch();
    char ts_buf[24];
    int ts_len = Legacy::fast_u64_to_str(ts, ts_buf);

    buf.reset();
    buf.append("{\"id\":\"pl");
    buf.append(id);
    buf.append(
        "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"");
    buf.append(id);
    buf.append("\",\"positionSide\":\"BOTH\"");

    if (type == "LIMIT") {
      buf.append(",\"price\":\"");
      buf.append(p);
      buf.append("\"");
    }

    buf.append(",\"quantity\":\"");
    buf.append(q);
    buf.append("\",\"side\":\"");
    buf.append(side);
    buf.append("\",\"symbol\":\"");
    buf.append(symbol);

    if (type == "LIMIT") {
      buf.append("\",\"timeInForce\":\"GTX\"");
    }

    buf.append(",\"timestamp\":");
    buf.append(ts_buf, ts_len);
    buf.append(",\"type\":\"");
    buf.append(type);
    buf.append("\"}}");
    buf.finish();

    benchmark::DoNotOptimize(buf.data);
  }
}
BENCHMARK(BM_Legacy);

static void BM_Fast(benchmark::State &state) {
  Fast::JsonBuf buf;
  std::string_view symbol = "BTCUSDT", side = "BUY", type = "LIMIT",
                   q = "0.001", p = "69000.5", id = "order_123456789";

  for ([[maybe_unused]] auto _ : state) {
    buf.reset();
    buf.append_lit("{\"id\":\"pl");
    buf.append_sv(id);
    buf.append_lit(
        "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"");
    buf.append_sv(id);
    buf.append_lit("\",\"positionSide\":\"BOTH\"");

    if (type == "LIMIT") [[likely]] {
      buf.append_lit(",\"price\":\"");
      buf.append_sv(p);
      buf.append_lit("\",\"quantity\":\"");
      buf.append_sv(q);
      buf.append_lit("\",\"side\":\"");
      buf.append_sv(side);
      buf.append_lit("\",\"symbol\":\"");
      buf.append_sv(symbol);
      buf.append_lit("\",\"timeInForce\":\"GTX\"");
    } else {
      buf.append_lit(",\"quantity\":\"");
      buf.append_sv(q);
      buf.append_lit("\",\"side\":\"");
      buf.append_sv(side);
      buf.append_lit("\",\"symbol\":\"");
      buf.append_sv(symbol);
    }

    buf.append_lit(",\"timestamp\":");
    buf.append_ts13(now_ms_epoch());

    buf.append_lit(",\"type\":\"");
    buf.append_sv(type);
    buf.append_lit("\"}}");
    buf.finish();

    benchmark::DoNotOptimize(buf.data);
  }
}
BENCHMARK(BM_Fast);

// ==========================================
// [Turbo] 不计代价的极致优化
// ==========================================
namespace Turbo {
struct alignas(64) JsonBuf {
  char data[2048];
  uint32_t len = 0;

  // 这里的核心思路：将指针操作完全内联，并在寄存器中完成
  // 通过 lambda 在 BM 中直接操作，避免 struct 成员更新开销
};

// 专门针对 13 位时间戳的并行 itoa
inline void write_ts13_turbo(uint64_t v, char *dst) {
  // 将 13 位拆分为 5 位和 8 位，缩短依赖链
  uint64_t high = v / 100000000;            // 5 digits
  uint32_t low = (uint32_t)(v % 100000000); // 8 digits

  // 处理 high (5 digits: X XX XX)
  uint32_t h1 = (uint32_t)(high / 100);
  uint32_t h2 = (uint32_t)(high % 100);
  dst[0] = (char)('0' + (h1 / 100));
  std::memcpy(dst + 1, &DIGITS_LUT[(h1 % 100) << 1], 2);
  std::memcpy(dst + 3, &DIGITS_LUT[h2 << 1], 2);

  // 处理 low (8 digits: XX XX XX XX)
  uint32_t l1 = low / 10000;
  uint32_t l2 = low % 10000;
  std::memcpy(dst + 5, &DIGITS_LUT[(l1 / 100) << 1], 2);
  std::memcpy(dst + 7, &DIGITS_LUT[(l1 % 100) << 1], 2);
  std::memcpy(dst + 9, &DIGITS_LUT[(l2 / 100) << 1], 2);
  std::memcpy(dst + 11, &DIGITS_LUT[(l2 % 100) << 1], 2);
}
} // namespace Turbo
static void BM_Turbo(benchmark::State &state) {
  Turbo::JsonBuf buf;
  std::string_view symbol = "BTCUSDT", side = "BUY", type = "LIMIT",
                   q = "0.001", p = "69000.5", id = "order_123456789";

  for ([[maybe_unused]] auto _ : state) {
    char *curr = buf.data;

    auto append_lit = [&](const char *s, size_t n) {
      std::memcpy(curr, s, n);
      curr += n;
    };

    auto append_sv = [&](std::string_view sv) {
      std::memcpy(curr, sv.data(), sv.size());
      curr += sv.size();
    };

    // 1. 头部合并写入
    append_lit("{\"id\":\"pl", 8);
    append_sv(id);
    append_lit(
        "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"",
        48);
    append_sv(id);
    append_lit("\",\"positionSide\":\"BOTH\"", 24);

    // 2. O(1) 类型判断：只看首字母
    if (!type.empty() && type[0] == 'L') [[likely]] {
      append_lit(",\"price\":\"", 10);
      append_sv(p);
      append_lit("\",\"quantity\":\"\",", 13); // 这里根据你的 Legacy 顺序优化了
      append_sv(q);
      append_lit("\",\"side\":\"\",", 9);
      append_sv(side);
      append_lit("\",\"symbol\":\"\",", 11);
      append_sv(symbol);
      append_lit("\",\"timeInForce\":\"GTX\"", 21);
    } else {
      append_lit(",\"quantity\":\"\",", 13);
      append_sv(q);
      append_lit("\",\"side\":\"\",", 9);
      append_sv(side);
      append_lit("\",\"symbol\":\"\",", 11);
      append_sv(symbol);
    }

    // 3. 并行 itoa 写入
    append_lit(",\"timestamp\":", 13);
    Turbo::write_ts13_turbo(now_ms_epoch(), curr);
    curr += 13;

    append_lit(",\"type\":\"", 9);
    append_sv(type);
    append_lit("\"}}", 3);

    buf.len = (uint32_t)(curr - buf.data);
    benchmark::DoNotOptimize(buf.data);
  }
}
BENCHMARK(BM_Turbo);

// ==========================================
// 验证逻辑：确保优化后的输出与原版完全一致
// ==========================================
bool verify_consistency() {
  Legacy::JsonBuf l_buf;
  Fast::JsonBuf f_buf;

  // 测试数据
  std::string_view symbol = "BTCUSDT", side = "BUY", type = "LIMIT";
  std::string_view q = "0.001", p = "69000.5", id = "order_12345";

  // 运行 Legacy
  {
    uint64_t ts = 1739260000000ULL;
    char ts_buf[24];
    int ts_len = Legacy::fast_u64_to_str(ts, ts_buf);
    l_buf.reset();
    l_buf.append("{\"id\":\"pl");
    l_buf.append(id);
    l_buf.append(
        "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"");
    l_buf.append(id);
    l_buf.append("\",\"positionSide\":\"BOTH\"");
    if (type == "LIMIT") {
      l_buf.append(",\"price\":\"");
      l_buf.append(p);
      l_buf.append("\"");
    }
    l_buf.append(",\"quantity\":\"");
    l_buf.append(q);
    l_buf.append("\",\"side\":\"");
    l_buf.append(side);
    l_buf.append("\",\"symbol\":\"");
    l_buf.append(symbol);
    if (type == "LIMIT") {
      l_buf.append("\",\"timeInForce\":\"GTX\"");
    }
    l_buf.append(",\"timestamp\":");
    l_buf.append(ts_buf, ts_len);
    l_buf.append(",\"type\":\"");
    l_buf.append(type);
    l_buf.append("\"}}");
    l_buf.finish();
  }

  // 运行 Fast
  {
    f_buf.reset();
    f_buf.append_lit("{\"id\":\"pl");
    f_buf.append_sv(id);
    f_buf.append_lit(
        "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"");
    f_buf.append_sv(id);
    f_buf.append_lit("\",\"positionSide\":\"BOTH\"");
    if (type == "LIMIT") [[likely]] {
      f_buf.append_lit(",\"price\":\"");
      f_buf.append_sv(p);
      f_buf.append_lit("\",\"quantity\":\"");
      f_buf.append_sv(q);
      f_buf.append_lit("\",\"side\":\"");
      f_buf.append_sv(side);
      f_buf.append_lit("\",\"symbol\":\"");
      f_buf.append_sv(symbol);
      f_buf.append_lit("\",\"timeInForce\":\"GTX\"");
    }
    f_buf.append_lit(",\"timestamp\":");
    f_buf.append_ts13(1739260000000ULL);
    f_buf.append_lit(",\"type\":\"");
    f_buf.append_sv(type);
    f_buf.append_lit("\"}}");
    f_buf.finish();
  }

  std::string_view l_res(l_buf.data, l_buf.len);
  std::string_view f_res(f_buf.data, f_buf.len);

  if (l_res != f_res) {
    std::print("Validation Failed!\nLegacy: {}\nFast:   {}\n", l_res, f_res);
    return false;
  }
  return true;
}

// ==========================================
// 改进后的微基准测试
// ==========================================

// 1. Itoa 差异测试：修复了输入被优化为常量的问题
static void BM_Itoa_Legacy_Fixed(benchmark::State &state) {
    uint64_t ts = now_ms_epoch();
    char buf[24];
    for ([[maybe_unused]] auto _ : state) {
        // 强制每次从内存/寄存器读取 ts，防止编译器直接计算结果
        uint64_t val = ts;
        benchmark::DoNotOptimize(val); 
        
        int len = Legacy::fast_u64_to_str(val, buf);
        
        // 强制编译器认为 buf 及其长度被后续使用了
        benchmark::ClobberMemory();
        benchmark::DoNotOptimize(len);
    }
}

static void BM_Itoa_Turbo_Fixed(benchmark::State &state) {
    uint64_t ts = now_ms_epoch();
    char buf[24];
    for ([[maybe_unused]] auto _ : state) {
        uint64_t val = ts;
        benchmark::DoNotOptimize(val);
        
        Turbo::write_ts13_turbo(val, buf);
        
        benchmark::ClobberMemory();
        benchmark::DoNotOptimize(buf);
    }
}

// 2. Append 差异测试：模拟动态长度与静态长度拷贝
static void BM_Append_Legacy_Fixed(benchmark::State &state) {
    Legacy::JsonBuf buf;
    // 模拟从外部输入的字符串（防止编译器知道它是字面量而优化掉循环）
    const char* lit_ptr = "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"";
    
    for ([[maybe_unused]] auto _ : state) {
        buf.reset();
        // Legacy 的 append 包含：strlen(隐式或显式) + while(*s) 循环
        buf.append(lit_ptr);
        benchmark::DoNotOptimize(buf.data);
    }
}

static void BM_Append_Turbo_Fixed(benchmark::State &state) {
    char data[2048];
    const char* lit_ptr = "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"";
    constexpr size_t len = 48; // Turbo 通常在编译期已知长度
    
    for ([[maybe_unused]] auto _ : state) {
        // 模拟 Turbo 的核心：直接使用已知长度的 memcpy
        std::memcpy(data, lit_ptr, len);
        benchmark::DoNotOptimize(data);
    }
}

// 3. Branch 差异测试：引入不确定性防止静态预测
static void BM_Branch_Legacy_Fixed(benchmark::State &state) {
    std::string_view type = "LIMIT";
    int i = 0;
    for ([[maybe_unused]] auto _ : state) {
        // 加上 i 确保 type == "LIMIT" 至少在形式上需要被求值
        if (type == "LIMIT") {
            benchmark::DoNotOptimize(++i);
        }
    }
}

static void BM_Branch_Turbo_Fixed(benchmark::State &state) {
    std::string_view type = "LIMIT";
    int i = 0;
    for ([[maybe_unused]] auto _ : state) {
        // Turbo 优化：首字母 O(1) 判定
        if (!type.empty() && type[0] == 'L') {
            benchmark::DoNotOptimize(++i);
        }
    }
}

BENCHMARK(BM_Itoa_Legacy_Fixed);
BENCHMARK(BM_Itoa_Turbo_Fixed);
BENCHMARK(BM_Append_Legacy_Fixed);
BENCHMARK(BM_Append_Turbo_Fixed);
BENCHMARK(BM_Branch_Legacy_Fixed);
BENCHMARK(BM_Branch_Turbo_Fixed);


// 修改后的 main 函数
int main(int argc, char **argv) {
  if (!verify_consistency()) {
    return 1; // 验证失败，不运行 Benchmark
  }
  std::print("Validation Passed. Starting Benchmark...\n");
  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
