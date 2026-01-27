#pragma once

#include "types.hpp"
#include <atomic>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <unistd.h>
#include <utility>

namespace eph {

template <typename T>
  requires ShmLayout<T>
class SharedMemory {
public:
  SharedMemory(std::string name, bool is_owner)
      : name_(std::move(name)), is_owner_(is_owner) {
    try {
      open_and_map();
    } catch (...) {
      cleanup();
      throw;
    }
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

  [[nodiscard]] T *get() noexcept { return &layout_->data; }
  [[nodiscard]] const T *get() const noexcept { return &layout_->data; }
  [[nodiscard]] const std::string &name() const noexcept { return name_; }

  T *operator->() noexcept { return get(); }
  const T *operator->() const noexcept { return get(); }

private:
  struct Layout {
    alignas(config::CACHE_LINE_SIZE) std::atomic<bool> initialized{false};

    alignas(alignof(T) > config::CACHE_LINE_SIZE
                ? alignof(T)
                : config::CACHE_LINE_SIZE) T data;
  };

  std::string name_;
  int fd_ = -1;
  size_t size_ = 0;
  Layout *layout_ = nullptr;
  bool is_owner_ = false;

  void open_and_map() {
    if (!is_owner_ && !layout_->initialized.load(std::memory_order_acquire)) {

      throw std::runtime_error("Shared memory not initialized: " + name_);
    }

    int flags = O_RDWR;

    if (is_owner_) {
      // 先清理可能残留的旧共享内存，避免"上次崩溃未清理"的风险，保证本次启动环境干净
      shm_unlink(name_.c_str());

      // O_EXCL 确保原子创建
      flags |= O_CREAT | O_EXCL;
    }

    // 1. Open
    fd_ = shm_open(name_.c_str(), flags, 0600);
    if (fd_ == -1) {
      throw std::system_error(errno, std::generic_category(),
                              "shm_open failed: " + name_);
    }

    // 2. Truncate
    if (is_owner_) {
      if (ftruncate(fd_, sizeof(Layout)) == -1) {
        throw std::system_error(errno, std::generic_category(),
                                "ftruncate failed");
      }
    } else {
      // Consumer 检查大小，防止映射空文件会导致总线错误 (SIGBUS)。
      // 例如，owner 执行 shm_open 后马上挂起了，Consumer 会成功
      // mmap 但访问共享区域时会崩溃。
      struct stat s;
      if (fstat(fd_, &s) == -1) {
        throw std::system_error(errno, std::generic_category(), "fstat failed");
      }
      if (static_cast<size_t>(s.st_size) < sizeof(Layout)) {
        throw std::runtime_error("Shared memory size mismatch (too small)");
      }
    }

    // 3. Mmap
    void *addr = mmap(nullptr, sizeof(Layout), PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd_, 0);
    if (addr == MAP_FAILED) {
      throw std::system_error(errno, std::generic_category(), "mmap failed");
    }

    layout_ = static_cast<Layout *>(addr);

    // 4. Initialization Handshake
    if (is_owner_) {
      std::construct_at(&layout_->data);
      layout_->initialized.store(true, std::memory_order_release);
    }
  }

  void cleanup() noexcept {
    if (layout_) {
      if (is_owner_) {
        // 仅对非平凡析构类型调用析构函数
        if constexpr (!std::is_trivially_destructible_v<T>) {
          layout_->data.~T();
        }
      }
      munmap(layout_, sizeof(Layout));
      layout_ = nullptr;
    }
    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }

    // Owner 负责 unlink，确保文件系统不残留
    if (is_owner_ && !name_.empty()) {
      shm_unlink(name_.c_str());
    }
  }
};

} // namespace eph
