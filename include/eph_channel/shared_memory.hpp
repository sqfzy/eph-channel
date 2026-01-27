#pragma once

#include "platform.hpp"
#include "types.hpp"
#include <atomic>
#include <chrono>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace eph {

// 共享内存头部，用于同步初始化状态
struct ShmHeader {
  alignas(config::CACHE_LINE_SIZE) std::atomic<bool> initialized{false};
};

template <typename T>
  requires ShmLayout<T>
class SharedMemory {
public:
  // 数据布局：Header + Data
  // 使用 byte array 避免 T 被意外构造，我们手动控制生命周期
  struct Layout {
    ShmHeader header;

    static constexpr size_t DataAlign = (alignof(T) > config::CACHE_LINE_SIZE)
                                            ? alignof(T)
                                            : config::CACHE_LINE_SIZE;

    alignas(DataAlign) T data;
  };

  SharedMemory(std::string name, bool create)
      : name_(std::move(name)), is_owner_(create) {
    open_and_map(create);
  }

  ~SharedMemory() { cleanup(); }

  SharedMemory(const SharedMemory &) = delete;
  SharedMemory &operator=(const SharedMemory &) = delete;

  SharedMemory(SharedMemory &&other) noexcept
      : name_(std::move(other.name_)), fd_(other.fd_), layout_(other.layout_),
        is_owner_(other.is_owner_) {
    other.fd_ = -1;
    other.layout_ = nullptr;
    other.is_owner_ = false;
  }

  SharedMemory &operator=(SharedMemory &&other) noexcept {
    if (this != &other) {
      cleanup();
      name_ = std::move(other.name_);
      fd_ = other.fd_;
      layout_ = other.layout_;
      is_owner_ = other.is_owner_;
      other.fd_ = -1;
      other.layout_ = nullptr;
      other.is_owner_ = false;
    }
    return *this;
  }

  T *get() noexcept { return &layout_->data; }
  const T *get() const noexcept { return &layout_->data; }

  T *operator->() noexcept { return get(); }
  const T *operator->() const noexcept { return get(); }

  const std::string &name() const noexcept { return name_; }

private:
  std::string name_;
  int fd_ = -1;
  Layout *layout_ = nullptr;
  bool is_owner_ = false;

  void open_and_map(bool create) {
    int flags = O_RDWR;
    if (create)
      flags |= O_CREAT;

    // 1. Open
    fd_ = shm_open(name_.c_str(), flags, 0666);
    if (fd_ == -1) {
      throw std::runtime_error("shm_open failed: " + name_);
    }

    // 2. Truncate (Owner only)
    if (create) {
      if (ftruncate(fd_, sizeof(Layout)) == -1) {
        ::close(fd_);
        throw std::runtime_error("ftruncate failed");
      }
    }

    // 3. Mmap
    void *addr = mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd_, 0);
    if (addr == MAP_FAILED) {
      ::close(fd_);
      throw std::runtime_error("mmap failed");
    }

    layout_ = static_cast<Layout *>(addr);

    // 4. Initialization Handshake
    if (create) {
      // Owner: Construct object locally first, then set initialized flag
      new (&layout_->data) T(); // Placement new

      // Release 语义确保 T 的构造在 flag 变为 true 之前对其他核心可见
      layout_->header.initialized.store(true, std::memory_order_release);
    } else {
      auto start = std::chrono::steady_clock::now();
      while (!layout_->header.initialized.load(std::memory_order_acquire)) {
        cpu_relax();
        if (std::chrono::steady_clock::now() - start >
            std::chrono::seconds(5)) {
          cleanup();
          throw std::runtime_error(
              "Timeout waiting for shared memory initialization");
        }
      }
    }
  }

  void cleanup() noexcept {
    if (layout_) {
      // 只有 owner 负责析构对象，但在 SHM 中，如果进程崩溃，
      // 对象可能不会被析构。对于 TriviallyCopyable 类型，通常不需要析构。
      // 如果 T 有复杂的清理逻辑（不应该有，因为是
      // TriviallyCopyable），这里需要注意。
      if (is_owner_) {
        layout_->data.~T();
      }
      munmap(layout_, sizeof(Layout));
      layout_ = nullptr;
    }
    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }
    if (is_owner_ && !name_.empty()) {
      shm_unlink(name_.c_str());
    }
  }
};

} 
