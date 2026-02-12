#include <benchmark/benchmark.h>
#include <cstdlib>
#include <cstring>
#include <vector>

#include "eph/benchmark/cpu_topology.hpp"
#include "eph/core/ring_buffer.hpp"

// ============================================================================
// 辅助工具：定长载荷 (Payload)
// ============================================================================

template <size_t Bytes> struct alignas(8) Payload {
  uint8_t data[Bytes];
  Payload() { std::memset(data, 0, Bytes); }
};

template <> struct alignas(8) Payload<8> {
  uint64_t v;
  Payload() : v(0) {}
};

// ============================================================================
// 1. 单线程 Push/Pop Overhead
// 对比：Value Copy (push/pop_latest) vs Zero Copy (write/try_read)
// ============================================================================

template <size_t PayloadSize, size_t BufSize>
static void BM_RingBuffer_PushPop(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  eph::RingBuffer<T, BufSize> rb;
  T in_val;
  T out_val;

  for ([[maybe_unused]] auto _ : state) {
    rb.push(in_val);
    rb.pop_latest(out_val);

    benchmark::DoNotOptimize(out_val);
    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * 2);
}

template <size_t PayloadSize, size_t BufSize>
static void BM_RingBuffer_ZeroCopy_PushPop(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  eph::RingBuffer<T, BufSize> rb;

  for ([[maybe_unused]] auto _ : state) {
    // Zero Copy Write
    rb.produce([](T &slot) {
      if constexpr (PayloadSize == 8)
        slot.v = 1;
      else
        slot.data[0] = 1;
    });

    // Zero Copy Read (单线程下无需自旋，直接使用 try_read)
    rb.consume_latest([](const T &slot) {
      if constexpr (PayloadSize == 8) {
        auto v = slot.v;
        benchmark::DoNotOptimize(v);
      } else {
        auto d = slot.data[0];
        benchmark::DoNotOptimize(d);
      }
    });

    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * 2);
}

// ============================================================================
// 2. 多线程 Ping-Pong 延迟 (Latency)
// ============================================================================

template <typename T, size_t BufSize> struct alignas(64) PingPongContext {
  eph::RingBuffer<T, BufSize> q1; // Thread 1 -> Thread 2
  char padding[64];
  eph::RingBuffer<T, BufSize> q2; // Thread 2 -> Thread 1
};

template <size_t PayloadSize, size_t BufSize>
static void BM_RingBuffer_PingPong(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  static PingPongContext<T, BufSize> *ctx = new PingPongContext<T, BufSize>();
  static auto topology = eph::benchmark::get_cpu_topology();

  if (topology.size() < 2) {
    state.SkipWithError("Need at least 2 cores");
    return;
  }

  if (state.thread_index() == 0) {
    eph::benchmark::set_thread_affinity(topology[0].hw_thread_id);
    T send_val;
    T recv_val;
    for ([[maybe_unused]] auto _ : state) {
      ctx->q1.push(send_val);
      ctx->q2.pop_latest(recv_val);
    }
  } else {
    eph::benchmark::set_thread_affinity(topology[1].hw_thread_id);
    T val;
    for ([[maybe_unused]] auto _ : state) {
      ctx->q1.pop_latest(val);
      ctx->q2.push(val);
    }
  }
}

template <size_t PayloadSize, size_t BufSize>
static void BM_RingBuffer_ZeroCopy_PingPong(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  static PingPongContext<T, BufSize> *ctx = new PingPongContext<T, BufSize>();
  static auto topology = eph::benchmark::get_cpu_topology();

  if (topology.size() < 2) {
    state.SkipWithError("Need at least 2 cores");
    return;
  }

  auto writer = [](T &slot) {
    if constexpr (PayloadSize == 8)
      slot.v = 1;
    else
      slot.data[0] = 1;
  };

  auto reader = [](const T &slot) {
    if constexpr (PayloadSize == 8) {
      auto v = slot.v;
      benchmark::DoNotOptimize(v);
    } else {
      auto d = slot.data[0];
      benchmark::DoNotOptimize(d);
    }
  };

  if (state.thread_index() == 0) {
    eph::benchmark::set_thread_affinity(topology[0].hw_thread_id);
    for ([[maybe_unused]] auto _ : state) {
      ctx->q1.produce(writer);
      ctx->q2.consume_latest(reader); // 阻塞式 Read
    }
  } else {
    eph::benchmark::set_thread_affinity(topology[1].hw_thread_id);
    for ([[maybe_unused]] auto _ : state) {
      ctx->q1.consume_latest(reader);
      ctx->q2.produce(writer);
    }
  }
}

// ============================================================================
// 3. 多线程 SPSC 吞吐量 (Throughput)
// ============================================================================

template <size_t PayloadSize, size_t BufSize>
static void BM_RingBuffer_Throughput(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  static eph::RingBuffer<T, BufSize> rb;
  static auto topology = eph::benchmark::get_cpu_topology();

  if (topology.size() < 2) {
    state.SkipWithError("Need at least 2 cores");
    return;
  }

  if (state.thread_index() == 0) {
    eph::benchmark::set_thread_affinity(topology[0].hw_thread_id);
    T val;
    for ([[maybe_unused]] auto _ : state) {
      rb.push(val);
      benchmark::ClobberMemory();
    }
  } else {
    eph::benchmark::set_thread_affinity(topology[1].hw_thread_id);
    T out;
    for ([[maybe_unused]] auto _ : state) {
      rb.pop_latest(out);
      benchmark::DoNotOptimize(out);
    }
  }
  state.SetItemsProcessed(state.iterations());
}

template <size_t PayloadSize, size_t BufSize>
static void BM_RingBuffer_ZeroCopy_Throughput(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  static eph::RingBuffer<T, BufSize> rb;
  static auto topology = eph::benchmark::get_cpu_topology();

  if (topology.size() < 2) {
    state.SkipWithError("Need at least 2 cores");
    return;
  }

  if (state.thread_index() == 0) {
    eph::benchmark::set_thread_affinity(topology[0].hw_thread_id);
    auto writer = [](T &slot) {
      if constexpr (PayloadSize == 8)
        slot.v = 1;
      else
        slot.data[0] = 1;
    };
    for ([[maybe_unused]] auto _ : state) {
      rb.produce(writer);
    }
  } else {
    eph::benchmark::set_thread_affinity(topology[1].hw_thread_id);
    auto reader = [](const T &slot) {
      if constexpr (PayloadSize == 8) {
        auto v = slot.v;
        benchmark::DoNotOptimize(v);
      } else {
        auto d = slot.data[0];
        benchmark::DoNotOptimize(d);
      }
    };
    for ([[maybe_unused]] auto _ : state) {
      rb.consume_latest(reader);
    }
  }
  state.SetItemsProcessed(state.iterations());
}

// ============================================================================
// Benchmark 注册宏
// ============================================================================

#define REGISTER_MATRIX(FUNC, ...)                                             \
  BENCHMARK_TEMPLATE(FUNC, 8, 8)->Name(#FUNC "/P:8/B:8") __VA_ARGS__;          \
  BENCHMARK_TEMPLATE(FUNC, 8, 64)->Name(#FUNC "/P:8/B:64") __VA_ARGS__;        \
  BENCHMARK_TEMPLATE(FUNC, 8, 512)->Name(#FUNC "/P:8/B:512") __VA_ARGS__;      \
  BENCHMARK_TEMPLATE(FUNC, 64, 8)->Name(#FUNC "/P:64/B:8") __VA_ARGS__;        \
  BENCHMARK_TEMPLATE(FUNC, 64, 64)->Name(#FUNC "/P:64/B:64") __VA_ARGS__;      \
  BENCHMARK_TEMPLATE(FUNC, 64, 512)->Name(#FUNC "/P:64/B:512") __VA_ARGS__;    \
  BENCHMARK_TEMPLATE(FUNC, 512, 8)->Name(#FUNC "/P:512/B:8") __VA_ARGS__;      \
  BENCHMARK_TEMPLATE(FUNC, 512, 64)->Name(#FUNC "/P:512/B:64") __VA_ARGS__;    \
  BENCHMARK_TEMPLATE(FUNC, 512, 512)->Name(#FUNC "/P:512/B:512") __VA_ARGS__

// 1. 单线程 Push/Pop
REGISTER_MATRIX(BM_RingBuffer_PushPop);
REGISTER_MATRIX(BM_RingBuffer_ZeroCopy_PushPop);

// 2. 多线程 Ping-Pong 延迟
REGISTER_MATRIX(BM_RingBuffer_PingPong, ->Threads(2)->UseRealTime());
REGISTER_MATRIX(BM_RingBuffer_ZeroCopy_PingPong, ->Threads(2)->UseRealTime());

// 3. 多线程 SPSC 吞吐量
REGISTER_MATRIX(BM_RingBuffer_Throughput, ->Threads(2)->UseRealTime());
REGISTER_MATRIX(BM_RingBuffer_ZeroCopy_Throughput, ->Threads(2)->UseRealTime());

BENCHMARK_MAIN();
