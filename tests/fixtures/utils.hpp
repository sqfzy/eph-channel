#pragma once

#include <gtest/gtest.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <fstream>
#include <unistd.h>
#include <thread>
#include <vector>

namespace eph::test {

// RAII 共享内存清理器
class ShmCleaner {
  std::string name_;
  bool should_cleanup_;

public:
  explicit ShmCleaner(std::string name, bool should_cleanup = true)
      : name_(std::move(name)), should_cleanup_(should_cleanup) {}

  ~ShmCleaner() {
    if (should_cleanup_) {
      shm_unlink(name_.c_str());
    }
  }

  ShmCleaner(const ShmCleaner&) = delete;
  ShmCleaner& operator=(const ShmCleaner&) = delete;

  const std::string& name() const { return name_; }
  void disable_cleanup() { should_cleanup_ = false; }
};

// 线程辅助工具
class ThreadRunner {
  std::vector<std::thread> threads_;

public:
  template <typename Func, typename... Args>
  void spawn(Func&& func, Args&&... args) {
    threads_.emplace_back(std::forward<Func>(func), std::forward<Args>(args)...);
  }

  void join_all() {
    for (auto& t : threads_) {
      if (t.joinable()) {
        t.join();
      }
    }
    threads_.clear();
  }

  ~ThreadRunner() {
    join_all();
  }
};

// 超时检测工具
template <typename Func>
bool run_with_timeout(Func&& func, std::chrono::milliseconds timeout) {
  std::atomic<bool> finished{false};
  std::thread worker([&]() {
    func();
    finished = true;
  });

  auto start = std::chrono::steady_clock::now();
  while (!finished) {
    if (std::chrono::steady_clock::now() - start > timeout) {
      worker.detach(); // 超时则分离线程
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
  }

  worker.join();
  return true;
}

// 验证内存对齐
template <typename T>
void verify_alignment(const T* ptr, size_t expected_alignment) {
  EXPECT_EQ(reinterpret_cast<uintptr_t>(ptr) % expected_alignment, 0)
      << "Pointer not aligned to " << expected_alignment << " bytes";
}

// 检查是否支持 HugePage
inline bool is_hugepage_available() {
  std::ifstream file("/proc/sys/vm/nr_hugepages");
  int count = 0;
  if (file >> count) {
    return count > 0;
  }
  return false;
}

// 进程 Fork 辅助类
class ForkedProcess {
  pid_t pid_ = -1;

public:
  enum class Role { Parent, Child };

  // Fork 并返回角色
  Role fork() {
    pid_ = ::fork();
    EXPECT_GE(pid_, 0) << "Fork failed: " << strerror(errno);
    return (pid_ == 0) ? Role::Child : Role::Parent;
  }

  // 子进程退出
  [[noreturn]] void child_exit(int status = 0) {
    ::exit(status);
  }

  // 父进程等待子进程
  int wait_child() {
    if (pid_ <= 0) return -1;
    int status = 0;
    waitpid(pid_, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
  }

  ~ForkedProcess() {
    if (pid_ > 0) {
      wait_child();
    }
  }
};

} // namespace eph::test
