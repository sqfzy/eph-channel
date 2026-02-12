#include <benchmark/benchmark.h>
#include <cstring>
#include <vector>

#include "eph/benchmark/cpu_topology.hpp"
#include "eph/core/queue.hpp"

// ============================================================================
// 辅助工具：定长载荷 (Payload)
// ============================================================================

// 使用模板生成指定字节大小的载荷
// alignas(8) 确保在 64位系统上的基本对齐，避免非对齐访问造成的额外开销干扰
template <size_t Bytes> struct alignas(8) Payload {
  uint8_t data[Bytes];

  // 提供默认构造，防止编译器认为变量未初始化而优化掉
  Payload() { std::memset(data, 0, Bytes); }
};

// 特化：对于 8 字节，直接模拟 uint64_t，更符合寄存器传参场景
template <> struct alignas(8) Payload<8> {
  uint64_t v;
  Payload() : v(0) {}
};

// ============================================================================
// 1. BoundedQueue_PushPop<PayloadSize, BufSize>
// 场景：单线程操作开销
// ============================================================================

template <size_t PayloadSize, size_t BufSize>
static void BM_BoundedQueue_PushPop(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  eph::BoundedQueue<T, BufSize> q;
  T val;
  T out;

  for ([[maybe_unused]] auto _ : state) {
    q.push(val);
    q.pop(out);

    benchmark::DoNotOptimize(out);
    benchmark::ClobberMemory();
  }

  state.SetItemsProcessed(state.iterations() * 2);
}

template <size_t PayloadSize, size_t BufSize>
static void BM_BoundedQueue_ZeroCopy_PushPop(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  eph::BoundedQueue<T, BufSize> q;

  for ([[maybe_unused]] auto _ : state) {
    q.produce([](T &slot) {
      if constexpr (PayloadSize == 8)
        slot.v = 1;
      else
        slot.data[0] = 1;
    });

    q.consume([](T &slot) {
      if constexpr (PayloadSize == 8)
        benchmark::DoNotOptimize(slot.v);
      else
        benchmark::DoNotOptimize(slot.data[0]);
    });

    benchmark::ClobberMemory();
  }
  state.SetItemsProcessed(state.iterations() * 2);
}

// ============================================================================
// 2. BoundedQueue_PingPong<PayloadSize, BufSize>
// 场景：SPSC 跨核心延迟 (Latency)
//
// 结构：T1 Push -> Q1 -> T2 Pop -> Q2 -> T1 Pop
// 测量缓存一致性协议 (MESI) 下的 Cache Miss 和总线通信延迟。
// ============================================================================

template <typename T, size_t BufSize> struct alignas(64) PingPongContext {
  // BufSize 现在随矩阵参数变化
  eph::BoundedQueue<T, BufSize> q1; // Thread 1 -> Thread 2
  char padding[64];                 // 强制隔离，防止伪共享 (False Sharing)
  eph::BoundedQueue<T, BufSize> q2; // Thread 2 -> Thread 1
};

template <size_t PayloadSize, size_t BufSize>
static void BM_BoundedQueue_PingPong(benchmark::State &state) {
  using T = Payload<PayloadSize>;

  // 静态分配，避免栈溢出并在多次迭代间复用
  static PingPongContext<T, BufSize> *ctx = new PingPongContext<T, BufSize>();
  static auto topology = eph::benchmark::get_cpu_topology();

  if (topology.size() < 2) {
    state.SkipWithError("Need at least 2 cores for PingPong test");
    return;
  }

  // 线程 0：Initiator (Ping)
  if (state.thread_index() == 0) {
    eph::benchmark::set_thread_affinity(topology[0].hw_thread_id);

    T send_val;
    T recv_val;

    for ([[maybe_unused]] auto _ : state) {
      // Step 1: Send to T2
      ctx->q1.push(send_val);

      // Step 4: Wait for echo from T2
      ctx->q2.pop(recv_val);
    }
  }
  // 线程 1：Echoer (Pong)
  else {
    eph::benchmark::set_thread_affinity(topology[1].hw_thread_id);

    T val;
    for ([[maybe_unused]] auto _ : state) {
      // Step 2: Wait for data from T1
      ctx->q1.pop(val);

      // Step 3: Echo back to T1
      ctx->q2.push(val);
    }
  }
}

template <size_t PayloadSize, size_t BufSize>
static void BM_BoundedQueue_ZeroCopy_PingPong(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  static PingPongContext<T, BufSize> *ctx = new PingPongContext<T, BufSize>();
  static auto topology = eph::benchmark::get_cpu_topology();

  if (topology.size() < 2) {
    state.SkipWithError("Need at least 2 cores");
    return;
  }

  // 辅助 Lambda：模拟最小化写入
  auto writer = [](T &slot) {
    if constexpr (PayloadSize == 8)
      slot.v = 1;
    else
      slot.data[0] = 1;
  };
  // 辅助 Lambda：模拟最小化读取
  auto reader = [](T &slot) {
    if constexpr (PayloadSize == 8)
      benchmark::DoNotOptimize(slot.v);
    else
      benchmark::DoNotOptimize(slot.data[0]);
  };

  if (state.thread_index() == 0) {
    eph::benchmark::set_thread_affinity(topology[0].hw_thread_id);
    for ([[maybe_unused]] auto _ : state) {
      ctx->q1.produce(writer); // Block wait
      ctx->q2.consume(reader); // Block wait
    }
  } else {
    eph::benchmark::set_thread_affinity(topology[1].hw_thread_id);
    for ([[maybe_unused]] auto _ : state) {
      ctx->q1.consume(reader);
      ctx->q2.produce(writer);
    }
  }
}

// ============================================================================
// 3. BoundedQueue_Throughput<PayloadSize, BufSize>
// 场景：SPSC 饱和吞吐量测试
// 生产者不停 Push，消费者不停 Pop，测量单位时间内处理的元素数量
// ============================================================================

template <size_t PayloadSize, size_t BufSize>
static void BM_BoundedQueue_Throughput(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  static eph::BoundedQueue<T, BufSize> q;
  static auto topology = eph::benchmark::get_cpu_topology();

  if (topology.size() < 2) {
    state.SkipWithError("Need at least 2 cores");
    return;
  }

  if (state.thread_index() == 0) {
    // 生产者线程
    eph::benchmark::set_thread_affinity(topology[0].hw_thread_id);
    T val;
    for ([[maybe_unused]] auto _ : state) {
      q.push(val);
      benchmark::ClobberMemory();
    }
  } else {
    // 消费者线程
    eph::benchmark::set_thread_affinity(topology[1].hw_thread_id);
    T out;
    for ([[maybe_unused]] auto _ : state) {
      q.pop(out);
      benchmark::DoNotOptimize(out);
    }
  }

  state.SetItemsProcessed(state.iterations());
}

template <size_t PayloadSize, size_t BufSize>
static void BM_BoundedQueue_ZeroCopy_Throughput(benchmark::State &state) {
  using T = Payload<PayloadSize>;
  static eph::BoundedQueue<T, BufSize> q;
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
      q.produce(writer);
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
      q.consume(reader);
    }
  }
  state.SetItemsProcessed(state.iterations());
}

// ============================================================================
// Benchmark 注册宏
// ============================================================================

// 按照 8, 64, 512 的矩阵进行注册
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
REGISTER_MATRIX(BM_BoundedQueue_PushPop);
REGISTER_MATRIX(BM_BoundedQueue_ZeroCopy_PushPop);

// 2. 多线程 Ping-Pong 延迟
REGISTER_MATRIX(BM_BoundedQueue_PingPong, ->Threads(2)->UseRealTime());
REGISTER_MATRIX(BM_BoundedQueue_ZeroCopy_PingPong, ->Threads(2)->UseRealTime());

// 3. 多线程 SPSC 吞吐量
REGISTER_MATRIX(BM_BoundedQueue_Throughput, ->Threads(2)->UseRealTime());
REGISTER_MATRIX(
    BM_BoundedQueue_ZeroCopy_Throughput, ->Threads(2)->UseRealTime());

BENCHMARK_MAIN();
