#pragma once

#include <eph/benchmark/recorder.hpp>
#include <eph/benchmark/timer.hpp>
#include <eph/platform.hpp>

#include <cstddef>
#include <string>
#include <tabulate/table.hpp>

using namespace eph::benchmark;
using namespace std::chrono_literals;

using AAOD_Type = std::variant<size_t, std::chrono::seconds>;

// 辅助函数：从字符串解析配置
inline AAOD_Type load_limit() {
  const char *env_p = std::getenv("AAOD_LIMIT");
  if (!env_p) {
    return size_t(100'000'000); // 默认值
  }

  std::string_view sv(env_p);

  // 如果以 's' 结尾，解析为秒
  if (sv.ends_with('s')) {
    size_t val = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size() - 1, val);
    if (ec == std::errc{})
      return std::chrono::seconds(val);
  } else {
    // 否则解析为 size_t
    size_t val = 0;
    auto [ptr, ec] = std::from_chars(sv.data(), sv.data() + sv.size(), val);
    if (ec == std::errc{})
      return val;
  }

  std::print("Warning: Failed to parse AAOD_LIMIT, using default.\n");
  return size_t(100'000'000);
}

// 运行时初始化
const AAOD_Type AAOD_LIMIT = load_limit();

template <size_t Bytes> struct MockData {
  std::array<std::byte, Bytes> payload;
};

using DataSizeList = std::integer_sequence<size_t, 64, 256, 1024>;
using CapacityList = std::integer_sequence<size_t, 2, 64, 4096>;

struct MatrixRecord {
  size_t data_size;
  size_t buf_size;
  Stats stats;
};

// 通用正交测试运行器
// Kernel 参数：一个接受 <size_t DataSize, size_t BufSize> 模板参数的 Lambda
template <size_t... Ds, size_t... Bs, typename Kernel>
void run_benchmark_matrix(std::string_view title,
                          std::integer_sequence<size_t, Ds...>,
                          std::integer_sequence<size_t, Bs...>,
                          Kernel &&kernel) {
  using namespace tabulate;
  Table table;

  // 1. 设置表头：第一列是 Label，后面跟着所有的 Buffer Size
  table.add_row(
      Table::Row_t{"DataSize \\ BufSize", (std::to_string(Bs) + "B")...});

  // 2. 外层循环：遍历 Data Size (Ds)，每一轮生成表格的一行
  (
      [&]<size_t D>() {
        Table::Row_t row;
        // 行首：显示当前的 Data Size
        row.push_back(std::to_string(D) + "B");

        // 3. 内层循环：遍历 Buffer Size (Bs)，填充该行的每一列数据
        (
            [&]<size_t B>() {
              // 核心调用：此时 D 和 B 都是编译期常量
              auto s = kernel.template operator()<D, B>();
              row.push_back(std::format("{:.2f} ns", s.avg_ns));
            }.template operator()<Bs>(), // 显式实例化内层 Lambda
            ...                          // 展开 Bs
        );

        // 将构建好的一行加入表格
        table.add_row(row);
      }.template operator()<Ds>(), // 显式实例化外层 Lambda
      ...                          // 展开 Ds
  );

  // --- 下面是样式设置，保持不变 ---
  table[0]
      .format()
      .font_align(FontAlign::center)
      .font_style({FontStyle::bold})
      .font_color(Color::yellow);

  table.column(0)
      .format()
      .font_color(Color::cyan)
      .font_style({FontStyle::bold});

  std::cout << "\n[" << title << " Matrix]\n" << table << std::endl;
}
