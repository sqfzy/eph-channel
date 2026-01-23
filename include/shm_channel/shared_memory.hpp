#pragma once

#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace shm {

// 共享内存 RAII 管理
template <typename T> class SharedMemory {
public:
  SharedMemory(const std::string &name, bool create)
      : name_(name), is_owner_(create) {
    open_and_map(create);
  }

  ~SharedMemory() { cleanup(); }

  // 禁止拷贝
  SharedMemory(const SharedMemory &) = delete;
  SharedMemory &operator=(const SharedMemory &) = delete;

  // 允许移动
  SharedMemory(SharedMemory &&other) noexcept
      : name_(std::move(other.name_)), fd_(other.fd_), ptr_(other.ptr_),
        is_owner_(other.is_owner_) {
    other.fd_ = -1;
    other.ptr_ = nullptr;
    other.is_owner_ = false;
  }

  SharedMemory &operator=(SharedMemory &&other) noexcept {
    if (this != &other) {
      cleanup();
      name_ = std::move(other.name_);
      fd_ = other.fd_;
      ptr_ = other.ptr_;
      is_owner_ = other.is_owner_;

      other.fd_ = -1;
      other.ptr_ = nullptr;
      other.is_owner_ = false;
    }
    return *this;
  }

  T *get() noexcept { return ptr_; }
  const T *get() const noexcept { return ptr_; }

  T *operator->() noexcept { return ptr_; }
  const T *operator->() const noexcept { return ptr_; }

  T &operator*() noexcept { return *ptr_; }
  const T &operator*() const noexcept { return *ptr_; }

  const std::string &name() const noexcept { return name_; }
  bool is_owner() const noexcept { return is_owner_; }

private:
  std::string name_;
  int fd_ = -1;
  T *ptr_ = nullptr;
  bool is_owner_ = false;

  void open_and_map(bool create) {
    int flags = O_RDWR;
    if (create)
      flags |= O_CREAT;

    fd_ = shm_open(name_.c_str(), flags, 0666);
    if (fd_ == -1) {
      throw std::runtime_error("Failed to open shared memory: " + name_);
    }

    if (create && ftruncate(fd_, sizeof(T)) == -1) {
      close(fd_);
      throw std::runtime_error("Failed to set shared memory size");
    }

    void *addr =
        mmap(nullptr, sizeof(T), PROT_READ | PROT_WRITE, MAP_SHARED, fd_, 0);
    if (addr == MAP_FAILED) {
      close(fd_);
      throw std::runtime_error("Failed to map shared memory");
    }

    if (create) {
      ptr_ = new (addr) T(); // Placement new
    } else {
      ptr_ = static_cast<T *>(addr);
    }
  }

  void cleanup() noexcept {
    if (ptr_) {
      munmap(ptr_, sizeof(T));
      ptr_ = nullptr;
    }
    if (fd_ != -1) {
      close(fd_);
      fd_ = -1;
    }
    if (is_owner_ && !name_.empty()) {
      shm_unlink(name_.c_str());
    }
  }
};

} // namespace shm
