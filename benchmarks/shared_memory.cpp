#include "common.hpp"
#include "eph/benchmark/recorder.hpp"
#include "eph/benchmark/timer.hpp"
#include "eph/core/queue.hpp"
#include "eph/core/shared_memory.hpp"
#include <csignal>
#include <format>
#include <print>
#include <sys/wait.h>
#include <unistd.h>

using namespace eph;
using namespace eph::benchmark;

// 场景: 跨进程 Ping-Pong
template <typename T, size_t N>
Stats bench_ipc_ping_pong(std::string name) {
  // 构造唯一的 SHM 名称，避免多轮测试冲突
  std::string shm_p2c_name = std::format("/bench_shm_p2c_{}", getpid());
  std::string shm_c2p_name = std::format("/bench_shm_c2p_{}", getpid());
  
  // 是否使用大页 (Huge Pages) 取决于系统配置，这里默认关闭以确保通用性
  // 如果系统开启了 hugepages，设为 true 可以进一步降低 TLB Miss
  bool use_huge_pages = false;

  // 1. 父进程初始化 SharedMemory (Owner)
  // SharedMemory 的构造函数会自动 unlink 旧文件并创建新文件
  auto p2c_owner = SharedMemory<BoundedQueue<T, N>>::create(shm_p2c_name, use_huge_pages);
  auto c2p_owner = SharedMemory<BoundedQueue<T, N>>::create(shm_c2p_name, use_huge_pages);

  // 2. Fork 创建消费者进程
  pid_t pid = fork();
  if (pid < 0) {
    std::println(stderr, "Fork failed: {}", strerror(errno));
    std::terminate();
  }

  if (pid == 0) {
    // =========================================================
    // Child Process (Consumer)
    // =========================================================
    try {
      bind_cpu(2);

      // 打开已存在的共享内存 (非 Owner)
      auto q_p2c = SharedMemory<BoundedQueue<T, N>>::open(shm_p2c_name, use_huge_pages);
      auto q_c2p = SharedMemory<BoundedQueue<T, N>>::open(shm_c2p_name, use_huge_pages);

      T tmp;
      // Consumer 持续运行，直到被父进程 Kill
      while (true) {
        // 忙等待接收 (P2C)
        while (!q_p2c->try_pop(tmp)) {
          eph::cpu_relax();
        }
        // 忙等待发送 (C2P)
        while (!q_c2p->try_push(tmp)) {
          eph::cpu_relax();
        }
      }
    } catch (const std::exception& e) {
      // 忽略子进程异常，直接退出
      std::exit(1);
    }
    std::exit(0);
  } else {
    // =========================================================
    // Parent Process (Producer)
    // =========================================================
    bind_cpu(3);
    T payload{};

    // 运行基准测试
    auto stats = run_bench(
        name,
        [&] {
          // 发送 P2C
          while (!p2c_owner->try_push(payload)) {
            eph::cpu_relax();
          }
          // 接收 C2P (等待回执)
          T ack;
          while (!c2p_owner->try_pop(ack)) {
            eph::cpu_relax();
          }
          do_not_optimize(ack);
        },
        {.limit = 5s});

    // 测试结束，清理子进程
    kill(pid, SIGTERM);
    waitpid(pid, nullptr, 0);

    return stats;
  }
}

int main() {
  TSC::init();

  std::string title = "shm_ipc_ping_pong_rtt";
  run_benchmark_matrix(title, DataSizeList{}, CapacityList{},
                       [&]<size_t D, size_t C>() {
                         using DataType = MockData<D>;
                         std::string suffix = std::format("_D{}_C{}", D, C);
                         return bench_ipc_ping_pong<DataType, C>(title + suffix);
                       });

  return 0;
}
