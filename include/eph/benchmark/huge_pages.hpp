#pragma once

#include <memory>
#include <cstdlib>
#include <cstring>
#include <print>

#if defined(__linux__)
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace eph::benchmark {

class HugePageAllocator {
public:
    template<typename T, typename... Args>
    static std::unique_ptr<T> create(Args&&... args) {
        void* ptr = allocate(sizeof(T), alignof(T));
        if (!ptr) {
            std::println(stderr, "Warning: HugePage allocation failed, using standard malloc");
            ptr = std::aligned_alloc(alignof(T), sizeof(T));
        }
        return std::unique_ptr<T>(new(ptr) T(std::forward<Args>(args)...), [](T* p) {
            p->~T();
            deallocate(p, sizeof(T));
        });
    }

private:
    static void* allocate(size_t size, size_t align) {
#if defined(__linux__)
        // 尝试 1GB 大页
        void* ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                         MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB | (30 << MAP_HUGE_SHIFT), -1, 0);
        if (ptr != MAP_FAILED) return ptr;

        // 回退到 2MB 大页
        ptr = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);
        if (ptr != MAP_FAILED) return ptr;
#endif
        return nullptr; // 失败，调用者回退到 std::aligned_alloc
    }

    static void deallocate(void* ptr, size_t size) {
#if defined(__linux__)
        munmap(ptr, size);
#else
        std::free(ptr);
#endif
    }
};

} // namespace eph::benchmark
