#pragma once

#include "types.hpp"
#include <atomic>
#include <cassert>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <system_error>
#include <thread>
#include <unistd.h>
#include <utility>

constexpr size_t HUGE_PAGE_SIZE = 2 * 1024 * 1024; // 2MB

namespace eph {

template <size_t Alignment> inline size_t align_up(size_t size) {
  static_assert(Alignment > 0 && (Alignment & (Alignment - 1)) == 0,
                "Alignment must be a power of 2");

  return (size + Alignment - 1) & ~(Alignment - 1);
}

/**
 * @brief 共享内存 (SHM) 的 RAII 封装
 */
template <typename T>
  requires ShmLayout<T>
class SharedMemory {
public:
  SharedMemory(std::string name, bool is_owner, bool use_huge_pages = false)
      : name_(std::move(name)), is_owner_(is_owner),
        use_huge_pages_(use_huge_pages) {
    resolve_full_path();
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
      : name_(std::move(other.name_)), full_path_(std::move(other.full_path_)),
        fd_(other.fd_), layout_(other.layout_), is_owner_(other.is_owner_) {
    other.fd_ = -1;
    other.layout_ = nullptr;
    other.is_owner_ = false;
  }

  SharedMemory &operator=(SharedMemory &&other) noexcept {
    if (this != &other) {
      cleanup();
      name_ = std::move(other.name_);
      full_path_ = std::move(other.full_path_);
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

    // 实际载荷数据
    // 强制对齐，防止 T 的起始地址未对齐导致的性能问题
    alignas(alignof(T) > config::CACHE_LINE_SIZE
                ? alignof(T)
                : config::CACHE_LINE_SIZE) T data;
  };

  std::string name_;
  std::string full_path_;
  int fd_ = -1;
  size_t map_size_ = 0;
  Layout *layout_ = nullptr;
  bool is_owner_ = false;
  bool use_huge_pages_ = false;

  void resolve_full_path() {
    std::string_view base_dir =
        use_huge_pages_ ? "/dev/hugepages/" : "/dev/shm/";

    // 处理 name 可能带有的前导 '/'，防止出现 //dev/shm//name
    std::string_view clean_name = name_;
    if (clean_name.starts_with('/')) {
      clean_name.remove_prefix(1);
    }

    full_path_ = std::string(base_dir) + std::string(clean_name);
  }

  void open_and_map() {
    int flags = O_RDWR;

    if (is_owner_) {
      ::unlink(full_path_.c_str());
      flags |= O_CREAT | O_EXCL;
    }

    // 1. Open
    fd_ = ::open(full_path_.c_str(), flags, S_IRUSR | S_IWUSR);
    if (fd_ == -1) {
      throw std::system_error(errno, std::generic_category(),
                              "open failed for: " + full_path_);
    }

    // 2. 计算对齐后的大小
    size_t raw_size = sizeof(Layout);
    if (use_huge_pages_) {
      // 向上取整到 2MB 的倍数
      map_size_ = align_up<HUGE_PAGE_SIZE>(raw_size);
    } else {
      map_size_ = raw_size;
    }

    // 3. Truncate & Check Size
    if (is_owner_) {
      if (ftruncate(fd_, map_size_) == -1) {
        throw std::system_error(errno, std::generic_category(),
                                "ftruncate failed");
      }
    } else {
      struct stat s;
      if (fstat(fd_, &s) == -1) {
        throw std::system_error(errno, std::generic_category(), "fstat failed");
      }
      // Consumer 检查大小，防止映射空文件会导致总线错误 (SIGBUS)。
      // 例如，owner 执行 shm_open 后马上挂起了，Consumer 会成功
      // mmap 但访问共享区域时会崩溃。
      if (static_cast<size_t>(s.st_size) < map_size_) {
        throw std::runtime_error("Shared memory size mismatch: file too small");
      }
    }

    // 4. Mmap
    int mmap_flags = MAP_SHARED;
    if (use_huge_pages_) {
      mmap_flags |= MAP_HUGETLB;
    }

    void *addr =
        mmap(nullptr, map_size_, PROT_READ | PROT_WRITE, mmap_flags, fd_, 0);

    if (addr == MAP_FAILED) {
      if (use_huge_pages_ && (errno == EINVAL || errno == ENOMEM)) {
        throw std::system_error(errno, std::generic_category(),
                                "mmap failed (Huge Pages enabled: check "
                                "/proc/sys/vm/nr_hugepages)");
      }
      throw std::system_error(errno, std::generic_category(), "mmap failed");
    }

    layout_ = static_cast<Layout *>(addr);

    // 5. Initialization
    if (is_owner_) {
      std::construct_at(&layout_->data);
      layout_->initialized.store(true, std::memory_order_release);
    } else {
      int retries = 0;
      while (!layout_->initialized.load(std::memory_order_acquire)) {
        if (++retries > 1000) {
          throw std::runtime_error("Shared memory initialization timeout: " +
                                   name_);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
      }
    }
  }

  void cleanup() noexcept {
    if (layout_) {
      if (is_owner_) {
        if constexpr (!std::is_trivially_destructible_v<T>) {
          layout_->data.~T();
        }
      }
      munmap(layout_, map_size_ > 0 ? map_size_ : sizeof(Layout));
      layout_ = nullptr;
    }

    if (fd_ != -1) {
      ::close(fd_);
      fd_ = -1;
    }

    // 统一清理逻辑：直接 unlink 完整路径
    if (is_owner_ && !full_path_.empty()) {
      ::unlink(full_path_.c_str());
    }
  }
};

} // namespace eph
