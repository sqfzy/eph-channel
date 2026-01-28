#include <cassert>
#include <chrono>
#include <iostream>
#include <numa.h>
#include <thread>
#include <vector>

// 简单的 RAII 包装器，用于管理 NUMA 内存
struct NumaMemory {
  void *ptr;
  size_t size;

  NumaMemory(size_t s, int node) : size(s) {
    // 在指定节点上分配内存
    ptr = numa_alloc_onnode(size, node);
    if (ptr == nullptr) {
      throw std::runtime_error("numa_alloc_onnode failed");
    }
  }

  ~NumaMemory() { numa_free(ptr, size); }

  // 禁止拷贝
  NumaMemory(const NumaMemory &) = delete;
  NumaMemory &operator=(const NumaMemory &) = delete;
};

void print_numa_info() {
  if (numa_available() < 0) {
    std::cerr << "System does not support NUMA API." << std::endl;
    return;
  }

  int max_node = numa_max_node();
  int num_cpus = numa_num_configured_cpus();

  std::cout << "NUMA Available: Yes\n"
            << "Max Node Index: " << max_node << "\n"
            << "Total Configured CPUs: " << num_cpus << "\n";

  // 打印每个节点的内存大小
  for (int i = 0; i <= max_node; ++i) {
    long long free_size;
    long long total_size = numa_node_size64(i, &free_size);
    std::cout << "Node " << i
              << ": Total Memory = " << (total_size / 1024 / 1024) << " MB\n";
  }
  std::cout << "--------------------------------\n";
}

int main() {
  // 1. 检查 NUMA 环境
  print_numa_info();

  if (numa_available() < 0) {
    return 1;
  }

  // 设定目标节点 (通常从 Node 0 开始)
  const int target_node = 0;
  const size_t data_size = 1024 * 1024 * 100; // 100 MB

  std::cout << "[Step 1] Allocating " << (data_size / 1024 / 1024)
            << "MB on Node " << target_node << "...\n";

  // 2. 在特定节点分配内存
  // 注意：标准 new/malloc 不保证特定节点，必须使用 numa_alloc_onnode
  NumaMemory data(data_size, target_node);

  // 3. 将当前线程绑定到该节点的 CPU
  std::cout << "[Step 2] Binding thread to Node " << target_node << "...\n";

  struct bitmask *mask = numa_allocate_nodemask();
  numa_bitmask_setbit(mask, target_node);

  // 绑定线程到节点
  numa_run_on_node(target_node);

  // 验证绑定是否成功
  int current_node = numa_preferred(); // 获取当前线程倾向的节点
  std::cout << "Current thread preferred node: " << current_node << "\n";

  // 释放 bitmask
  numa_free_nodemask(mask);

  // 4. 执行计算 (由于线程和内存都在同一节点，理论上延迟最低)
  std::cout << "[Step 3] Performing write operations...\n";

  auto start = std::chrono::high_resolution_clock::now();

  int *array = static_cast<int *>(data.ptr);
  size_t count = data_size / sizeof(int);

  // 简单的写入操作
  for (size_t i = 0; i < count; ++i) {
    array[i] = static_cast<int>(i);
  }

  auto end = std::chrono::high_resolution_clock::now();
  std::chrono::duration<double> elapsed = end - start;

  std::cout << "Operation completed in " << elapsed.count() << " seconds.\n";

  return 0;
}
