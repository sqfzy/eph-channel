#include "eph/core/json_buf.hpp"
#include "eph/platform.hpp"
#include <benchmark/benchmark.h>
#include <print>

inline constexpr std::string_view SYMBOL = "BTCUSDT";
inline constexpr std::string_view SIDE = "BUY";
inline constexpr std::string_view TYPE = "LIMIT";
inline constexpr std::string_view QTY = "0.001";
inline constexpr std::string_view PRICE = "69000.5";
inline constexpr std::string_view ID = "order_123456789";
inline constexpr uint64_t STATIC_TS = 1739260000000ULL;
inline constexpr std::string_view EXPECTED_JSON =
    "{\"id\":\"plorder_123456789\",\"method\":\"order.place\",\"params\":{"
    "\"newClientOrderId\":\"order_123456789\",\"positionSide\":\"BOTH\","
    "\"price\":\"69000.5\",\"quantity\":\"0.001\",\"side\":\"BUY\",\"symbol\":"
    "\"BTCUSDT\",\"timeInForce\":\"GTX\",\"type\":\"LIMIT\",\"timestamp\":"
    "1739260000000}}";

ALWAYS_INLINE void serialize_binance_place_order(
    eph::JsonBuf &buf, std::string_view symbol, std::string_view side,
    std::string_view type, std::string_view quantity, std::string_view price,
    std::string_view client_id, uint64_t timestamp) noexcept {
  buf.reset();

  buf.append_lit("{\"id\":\"pl");
  buf.append_sv(client_id);
  buf.append_lit(
      "\",\"method\":\"order.place\",\"params\":{\"newClientOrderId\":\"");
  buf.append_sv(client_id);
  buf.append_lit("\",\"positionSide\":\"BOTH\"");

  // 统一逻辑入口
  if (!type.empty() && type[0] == 'L') [[likely]] {
    buf.append_lit(",\"price\":\"");
    buf.append_sv(price);
    buf.append_lit("\",\"quantity\":\"");
    buf.append_sv(quantity);
    buf.append_lit("\",\"side\":\"");
    buf.append_sv(side);
    buf.append_lit("\",\"symbol\":\"");
    buf.append_sv(symbol);
    buf.append_lit("\",\"timeInForce\":\"GTX\",\"type\":\"LIMIT\"");
  } else {
    buf.append_lit(",\"quantity\":\"");
    buf.append_sv(quantity);
    buf.append_lit("\",\"side\":\"");
    buf.append_sv(side);
    buf.append_lit("\",\"symbol\":\"");
    buf.append_sv(symbol);
    buf.append_lit("\",\"type\":\"MARKET\"");
  }

  buf.append_lit(",\"timestamp\":");
  buf.append_ts13(timestamp);
  buf.append_lit("}}");
}

using namespace eph;

static void BM_Json_Buf_Serialize(benchmark::State &state) {
  JsonBuf buf;

  for ([[maybe_unused]] auto _ : state) {
    serialize_binance_place_order(buf, SYMBOL, SIDE, TYPE, QTY, PRICE, ID,
                                  STATIC_TS);

    benchmark::DoNotOptimize(buf.data);
  }
}
BENCHMARK(BM_Json_Buf_Serialize);

int main(int argc, char **argv) {
  JsonBuf buf;

  serialize_binance_place_order(buf, SYMBOL, SIDE, TYPE, QTY, PRICE, ID,
                                STATIC_TS);

  std::string_view result = buf.view();

  if (result != EXPECTED_JSON) {
    std::print("CRITICAL: Serialization Consistency Check Failed!\n");
    std::print("Expected: {}\n", EXPECTED_JSON);
    std::print("Actual  : {}\n", result);

    // 打印长度差异，辅助定位隐蔽的空字符或空格问题
    std::print("Length - Expected: {}, Actual: {}\n", EXPECTED_JSON.size(),
               result.size());
    return 1;
  }

  benchmark::Initialize(&argc, argv);
  benchmark::RunSpecifiedBenchmarks();
  return 0;
}
