#include <algorithm>
#include <benchmark/benchmark.h>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <numeric>
#include <random>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <vector>

static void BM_Memcpy(benchmark::State &state) {
  const size_t size = state.range(0);
  std::vector<char> src(size, 'x');
  std::vector<char> dst(size, '0');

  for ([[maybe_unused]] auto _ : state) {
    std::memcpy(dst.data(), src.data(), size);
    benchmark::ClobberMemory(); // 确保数据真的写到了内存（而不是只在缓存行）
  }
  state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(size));
}
BENCHMARK(BM_Memcpy)->RangeMultiplier(8)->Range(8, 64 * 1024 * 1024);

static void BM_MemoryLatency(benchmark::State &state) {
  const size_t size = state.range(0);
  const size_t num_elements = size / sizeof(void *);
  std::vector<void *> v(num_elements);

  // 1. 准备：创建一个包含所有索引的数组
  std::vector<size_t> indices(num_elements);
  std::iota(indices.begin(), indices.end(), 0);

  // 2. 核心：通过随机洗牌打乱访问顺序，让预取器失效
  std::mt19937 g(42);
  std::shuffle(indices.begin(), indices.end(), g);

  // 3. 将数组串联成一个巨大的循环链表
  for (size_t i = 0; i < num_elements - 1; ++i) {
    v[indices[i]] = &v[indices[i + 1]];
  }
  v[indices[num_elements - 1]] = &v[indices[0]];

  void *p = &v[indices[0]];
  for ([[maybe_unused]] auto _ : state) {
    // 这条指令是串行的：只有拿到 p 的值，才能知道下一个地址在哪里
    p = *(void **)p;
    benchmark::DoNotOptimize(p);
  }
}
// 范围从 8KB 到 128MB
BENCHMARK(BM_MemoryLatency)
    ->RangeMultiplier(4)
    ->Range(8 * 1024, 128 * 1024 * 1024);

static void BM_CPULatency_Dependency(benchmark::State &state) {
  int x = 1;
  for ([[maybe_unused]] auto _ : state) {
    // 每一行都依赖上一行的结果，强制 CPU 串行执行
    x = (x + 1) * (x + 1);
    x = (x + 1) * (x + 1);
    x = (x + 1) * (x + 1);
    benchmark::DoNotOptimize(x);
  }
}
BENCHMARK(BM_CPULatency_Dependency);

class IOBenchmark : public benchmark::Fixture {
public:
  int fd = -1;
  std::string test_file;
  const size_t file_size = 64 * 1024 * 1024; // 64MB，足够覆盖 L3 缓存
  const size_t align_size = 4096;
  bool using_direct_io = false;

  void SetUp(const ::benchmark::State &state) override {
    test_file = "benchmark_io_test_" + std::to_string(getpid()) + ".tmp";

    // 1. 先用普通模式打开并填满数据，确保没有空洞
    // 这一步至关重要！必须真实写入数据。
    int setup_fd = open(test_file.c_str(), O_RDWR | O_CREAT | O_TRUNC, 0644);
    if (setup_fd == -1)
      std::abort();

    // 创建一个随机数据缓冲区用于填充
    std::vector<char> buf(1024 * 1024); // 1MB buffer
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> dist(0, 255);
    for ([[maybe_unused]] auto &c : buf)
      c = static_cast<char>(dist(rng));

    // 填满文件
    for (size_t i = 0; i < file_size; i += buf.size()) {
      size_t write_len = std::min(buf.size(), file_size - i);
      if (write(setup_fd, buf.data(), write_len) != (ssize_t)write_len) {
        close(setup_fd);
        std::abort();
      }
    }
    fsync(setup_fd); // 强制刷盘
    close(setup_fd);

    // 2. 重新以 O_DIRECT 模式打开
    fd = open(test_file.c_str(), O_RDWR | O_DIRECT, 0644);
    if (fd != -1) {
      using_direct_io = true;
    } else {
      // 降级处理
      fd = open(test_file.c_str(), O_RDWR, 0644);
    }
  }

  void TearDown(const ::benchmark::State &state) override {
    if (fd != -1)
      close(fd);
    unlink(test_file.c_str());
  }
};

BENCHMARK_DEFINE_F(IOBenchmark, BM_RandomRead)(benchmark::State &state) {
  const size_t block_size = state.range(0);

  // 1. 内存对齐分配
  void *buffer = nullptr;
  if (posix_memalign(&buffer, align_size, block_size) != 0) {
    state.SkipWithError("posix_memalign failed");
    return;
  }

  // 2. 预先生成随机偏移量表 (Pre-computation)
  // 避免在计时循环中进行除法和随机数生成
  const int kNumOffsets = 4096;
  std::vector<size_t> offsets(kNumOffsets);
  std::mt19937 gen(42);
  // 确保 offset 是 block_size 的整数倍 (对齐要求)
  size_t max_blocks = file_size / block_size;
  std::uniform_int_distribution<size_t> dist(0, max_blocks - 1);

  for (int i = 0; i < kNumOffsets; ++i) {
    offsets[i] = dist(gen) * block_size;
  }

  size_t offset_index = 0;

  // 3. 核心测试循环
  for ([[maybe_unused]] auto _ : state) {
    // 极速查表
    size_t offset = offsets[offset_index];
    offset_index = (offset_index + 1) &
                   (kNumOffsets - 1); // 快速取模 (需 kNumOffsets 为 2 的幂)

    ssize_t ret = pread(fd, buffer, block_size, offset);
    if (ret != (ssize_t)block_size) {
      state.SkipWithError("pread failed");
      break;
    }
    benchmark::DoNotOptimize(buffer);
  }

  state.SetBytesProcessed(int64_t(state.iterations()) * int64_t(block_size));
  state.SetLabel(using_direct_io ? "DirectIO" : "BufferedIO");

  free(buffer);
}

BENCHMARK_REGISTER_F(IOBenchmark, BM_RandomRead)
    ->Arg(4096)
    ->Arg(16384)
    ->Arg(65536)
    ->Arg(1048576)
    ->UseRealTime();

BENCHMARK_MAIN();
