#pragma once

#include <eph/benchmark/recorder.hpp>
#include <eph/benchmark/timer.hpp>
#include <eph/platform.hpp>

#include <cstddef>
#include <print>
#include <string>
#include <tabulate/table.hpp>

using namespace eph::benchmark;
using namespace std::chrono_literals;

template <size_t Bytes> struct alignas(64) MockData {
  std::array<std::byte, Bytes> payload;
};

using DataSizeList = std::integer_sequence<size_t, 64, 256, 1024>;
using CapacityList = std::integer_sequence<size_t, 64, 1024, 65536>;

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

  table.add_row(Table::Row_t{"DataSize \\ BufSize", std::to_string(Bs)...});

  (
      [&] {
        Table::Row_t row;
        row.push_back(std::to_string(Ds));

        (
            [&] {
              auto s = kernel.template operator()<Ds, Bs>();
              row.push_back(std::format("{:.2f} ns", s.avg_ns));
            }(),
            ...);

        table.add_row(row);
      }(),
      ...);

  table[0]
      .format()
      .font_align(FontAlign::center)
      .font_style({FontStyle::bold})
      .font_color(Color::yellow);

  table.column(0)
      .format()
      .font_color(Color::cyan)
      .font_style({FontStyle::bold});

  std::println("\n>>> {} Matrix", title);
  std::cout << table << std::endl;
}
