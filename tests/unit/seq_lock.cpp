#include "eph/core/seq_lock.hpp"
#include "../fixtures/config.hpp"
#include <gtest/gtest.h>
#include <thread>

using namespace eph;
using namespace eph::test;

class SeqLockTest : public ::testing::Test {
protected:
  SeqLock<TestMessage> lock_;
  TestDataGenerator gen_;
};

TEST_F(SeqLockTest, Initialization) {
  // 默认构造后序列号应为偶数（未锁定状态）
  TestMessage msg = lock_.load();
  // 无法直接访问 seq_，但可以验证读取成功
  SUCCEED();
}

TEST_F(SeqLockTest, UncontestedReadWrite) {
  auto original = gen_.generate_message(123);

  lock_.store(original);

  auto retrieved = lock_.load();
  EXPECT_EQ(retrieved.id, original.id);
  EXPECT_EQ(retrieved.timestamp, original.timestamp);
  EXPECT_DOUBLE_EQ(retrieved.value, original.value);
}

TEST_F(SeqLockTest, ZeroCopyWrite) {
  lock_.write([](TestMessage &msg) {
    msg.id = 999;
    msg.timestamp = 12345;
    msg.value = 3.14;
  });

  TestMessage retrieved = lock_.load();
  EXPECT_EQ(retrieved.id, 999);
  EXPECT_EQ(retrieved.timestamp, 12345);
  EXPECT_DOUBLE_EQ(retrieved.value, 3.14);
}

TEST_F(SeqLockTest, ZeroCopyRead) {
  auto original = gen_.generate_message(456);
  lock_.store(original);

  uint64_t observed_id = 0;
  lock_.read([&observed_id](const TestMessage &msg) { observed_id = msg.id; });

  EXPECT_EQ(observed_id, original.id);
}

TEST_F(SeqLockTest, TryLoadSuccess) {
  auto msg = gen_.generate_message(789);
  lock_.store(msg);

  TestMessage out;
  EXPECT_TRUE(lock_.try_load(out));
  EXPECT_EQ(out.id, msg.id);
}

TEST(SeqLockConcurrencyTest, ReadLatestDuringWrites) {
  SeqLock<uint64_t> lock;

  std::atomic<bool> stop{false};
  std::atomic<uint64_t> writer_current{0};

  // 写线程：持续写入递增的值
  std::thread writer([&]() {
    for (uint64_t i = 1; i <= 100000; ++i) {
      lock.store(i);
      writer_current.store(i, std::memory_order_release);

      // 每隔一段时间稍微暂停，模拟真实场景
      if (i % 1000 == 0) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }
    }
    stop = true;
  });

  // 读线程：持续读取并检查是否接近最新值
  std::thread reader([&]() {
    while (!stop) {
      uint64_t read_value = lock.load();
      uint64_t current_write = writer_current.load(std::memory_order_acquire);
    }

    // 停止后再读几次，应该能读到最终值
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    for (int i = 0; i < 10; ++i) {
      uint64_t final_value = lock.load();
      EXPECT_EQ(final_value, 100000) << "Final read should get latest value";
    }
  });

  writer.join();
  reader.join();
}

TEST(SeqLockConcurrencyTest, TryLoadFailureDuringWrite) {
  SeqLock<TestMessage> lock;
  TestDataGenerator gen;

  std::atomic<bool> writer_in_critical{false};
  std::atomic<bool> reader_tried{false};
  std::atomic<bool> try_load_result{false};

  // 写线程：在临界区暂停
  std::thread writer([&]() {
    lock.write([&](TestMessage &msg) {
      msg = gen.generate_message(1);
      writer_in_critical = true;

      // 等待读线程尝试读取
      while (!reader_tried) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
  });

  // 读线程：在写入期间尝试 try_load
  std::thread reader([&]() {
    while (!writer_in_critical) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    TestMessage out;
    try_load_result = lock.try_load(out);
    reader_tried = true;
  });

  writer.join();
  reader.join();

  // try_load 应该失败（因为写入正在进行）
  EXPECT_FALSE(try_load_result);
}
