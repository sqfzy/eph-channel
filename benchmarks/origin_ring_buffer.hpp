#include <atomic>
#include <cstdint>
#include <cstring>

inline uint64_t now_ns_epoch2() {
#ifdef __linux__
  timespec ts;
  clock_gettime(CLOCK_REALTIME, &ts);
  return uint64_t(ts.tv_sec) * 1'000'000'000ull + uint64_t(ts.tv_nsec);
#else
  using namespace std::chrono;
  return (uint64_t)duration_cast<nanoseconds>(
             system_clock::now().time_since_epoch())
      .count();
#endif
}

template <uint32_t CAP_POW2, uint32_t SLOT_BYTES>
struct alignas(64) RingBuffer {
  static_assert((CAP_POW2 & (CAP_POW2 - 1)) == 0, "CAP must be power of two");

  struct alignas(64) Slot {
    uint32_t len;
    uint8_t data[SLOT_BYTES];
  };

  std::atomic<uint32_t> head{0}; // producer only writes (publish)
  std::atomic<uint32_t> tail{0}; // consumer only writes
  Slot slots[CAP_POW2];

  inline bool push(const uint8_t *p, uint32_t n) {
    uint32_t h = head.load(std::memory_order_relaxed);
    uint32_t t = tail.load(std::memory_order_acquire);
    if ((uint32_t)(h - t) == CAP_POW2)
      return false; // full

    Slot &s = slots[h & (CAP_POW2 - 1)];
    uint32_t ln = (n <= SLOT_BYTES) ? n : SLOT_BYTES;
    s.len = ln;
    // char temp[1024];
    // memcpy(temp, p, ln);
    // std::cerr<<"ori:"<<temp<<std::endl;
    std::memcpy(s.data, p, ln);

    head.store(h + 1, std::memory_order_release);
    return true;
  }

  inline bool pop(const uint8_t *&out, uint32_t &n) {
    uint32_t t = tail.load(std::memory_order_relaxed);
    uint32_t h = head.load(std::memory_order_acquire);
    if (t == h)
      return false; // empty

    Slot &s = slots[t & (CAP_POW2 - 1)];
    out = s.data;
    n = s.len;

    tail.store(t + 1, std::memory_order_release);
    return true;
  }

  // consumer: pop latest message, discard all intermediate messages
  // out: points to latest slot memory (copy it immediately if needed)
  // out_len: length of latest message
  // out_discarded: number of messages discarded (NOT including the latest);
  // optional
  inline bool pop_latest(const uint8_t *&out, uint32_t &out_len,
                         uint32_t *out_discarded = nullptr) {
    const uint64_t tt = now_ns_epoch2();
    // std::cerr<<"aa:"<<tt - ss<<std::endl;

    uint32_t t = tail.load(std::memory_order_relaxed);
    uint32_t h = head.load(std::memory_order_acquire);

    if (t == h) {
      if (out_discarded)
        *out_discarded = 0;

      return false; // empty
    }

    const uint64_t tt2 = now_ns_epoch2();
    // std::cerr<<"aa:"<<tt2 - tt<<std::endl;

    // available messages count
    // std::cerr<<h<<","<<t<<","<<h-t<<std::endl;
    uint32_t avail = (uint32_t)(h - t);

    // latest is at (h-1)
    Slot &s = slots[(h - 1) & (CAP_POW2 - 1)];
    out = s.data;
    out_len = s.len;

    // discard everything up to head
    tail.store(h, std::memory_order_release);

    // discarded count excludes the latest one
    if (out_discarded)
      *out_discarded = (avail > 0 ? (avail - 1) : 0);

    return true;
  }

  inline uint32_t size_approx() const {
    uint32_t h = head.load(std::memory_order_acquire);
    uint32_t t = tail.load(std::memory_order_acquire);
    return (uint32_t)(h - t);
  }
};
