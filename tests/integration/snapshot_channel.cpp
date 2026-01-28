#include "../fixtures/config.hpp"
#include "../fixtures/ipc_fixture.hpp"
#include "../fixtures/utils.hpp"
#include "eph_channel/channel.hpp"
#include <atomic>
#include <gtest/gtest.h>

using namespace eph::snapshot;
using namespace eph::test;

// ============================================================================
// ITC Snapshot 测试
// ============================================================================

class ItcSnapshotTest : public ::testing::Test {
protected:
  TestDataGenerator gen_;
};

TEST_F(ItcSnapshotTest, PublishSubscribe) {
  auto [pub, sub] = itc::channel<int>();

  pub.publish(42);

  int val = sub.fetch();
  EXPECT_EQ(val, 42);
}

TEST_F(ItcSnapshotTest, ConcurrentReadWrite) {
  auto [pub, sub] = itc::channel<TestMessage>();

  std::atomic<bool> stop{false};
  std::atomic<int> successful_reads{0};

  // 写线程：持续更新
  std::thread writer([&, p = std::move(pub)]() mutable {
    for (int i = 0; i < 10000; ++i) {
      auto msg = gen_.generate_message(i);
      p.publish(msg);
    }
    stop = true;
  });

  // 读线程：持续读取
  std::thread reader([&, s = std::move(sub)]() mutable {
    while (!stop) {
      TestMessage msg = s.fetch();
      successful_reads++;
    }
  });

  writer.join();
  reader.join();

  EXPECT_GT(successful_reads.load(), 0);
  std::cout << "Successful reads: " << successful_reads.load() << std::endl;
}

TEST_F(ItcSnapshotTest, TryFetchFailure) {
  auto [pub, sub] = itc::channel<LargeTestData>();

  std::atomic<bool> writer_in_critical{false};
  std::atomic<bool> reader_tried{false};
  std::atomic<int> try_fetch_failures{0};

  // 写线程：在写入期间暂停
  std::thread writer([&, p = std::move(pub)]() mutable {
    p.publish([&](LargeTestData &data) {
      data.sequence = 123;
      writer_in_critical = true;

      while (!reader_tried) {
        std::this_thread::sleep_for(std::chrono::microseconds(10));
      }

      std::this_thread::sleep_for(std::chrono::milliseconds(50));
    });
  });

  // 读线程：尝试在写入期间读取
  std::thread reader([&, s = std::move(sub)]() mutable {
    while (!writer_in_critical) {
      std::this_thread::sleep_for(std::chrono::microseconds(10));
    }

    // 尝试多次 try_fetch
    for (int i = 0; i < 10; ++i) {
      LargeTestData data;
      if (!s.try_fetch(data)) {
        try_fetch_failures++;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    reader_tried = true;
  });

  writer.join();
  reader.join();

  // 应该有一些 try_fetch 失败（因为写入正在进行）
  EXPECT_GT(try_fetch_failures.load(), 0);
  std::cout << "try_fetch failures: " << try_fetch_failures.load() << std::endl;
}

TEST_F(ItcSnapshotTest, ZeroCopyInterface) {
  auto [pub, sub] = itc::channel<TestMessage>();

  // 零拷贝发布
  pub.publish([](TestMessage &msg) {
    msg.id = 999;
    msg.timestamp = 12345;
    msg.value = 3.14;
  });

  // 零拷贝订阅
  uint64_t observed_id = 0;
  sub.fetch([&observed_id](const TestMessage &msg) { observed_id = msg.id; });

  EXPECT_EQ(observed_id, 999);
}

// ============================================================================
// IPC Snapshot 测试
// ============================================================================

class IpcSnapshotTest : public IpcTestFixture {
protected:
  TestDataGenerator gen_;
};

TEST_F(IpcSnapshotTest, CrossProcessSnapshot) {
  ForkedProcess fork_helper;

  auto [pub, sub] = ipc::channel<TestMessage>(shm_name_);

  if (fork_helper.fork() == ForkedProcess::Role::Child) {
    // 子进程：Subscriber
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto msg = sub.fetch();

    if (msg.id == 777) {
      fork_helper.child_exit(0);
    } else {
      fork_helper.child_exit(1);
    }
  } else {
    // 父进程：Publisher
    TestMessage msg{};
    msg.id = 777;
    msg.timestamp = 888;
    msg.value = 1.23;

    pub.publish(msg);

    EXPECT_EQ(fork_helper.wait_child(), 0);
  }
}

TEST_F(IpcSnapshotTest, LargeObjectHandling) {
  auto [pub, sub] = ipc::channel<LargeTestData>(shm_name_);

  auto data = gen_.generate_large_data(12345);
  pub.publish(data);

  auto retrieved = sub.fetch();
  EXPECT_EQ(retrieved, data);
}

TEST_F(IpcSnapshotTest, ContinuousUpdates) {
  ForkedProcess fork_helper;

  if (fork_helper.fork() == ForkedProcess::Role::Child) {
    auto [pub, sub] = ipc::channel<uint64_t>(shm_name_);

    uint64_t last_val = 0;
    for (int i = 0; i < 1000; ++i) {
      uint64_t val = sub.fetch();
      // 值应该单调递增（或相等）
      if (val < last_val) {
        fork_helper.child_exit(1);
      }
      last_val = val;
    }

    fork_helper.child_exit(0);
  } else {
    auto [pub, sub] = ipc::channel<uint64_t>(shm_name_);

    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    for (uint64_t i = 0; i < 10000; ++i) {
      pub.publish(i);
    }

    EXPECT_EQ(fork_helper.wait_child(), 0);
  }
}

TEST_F(IpcSnapshotTest, FetchTimeout) {
  auto [pub, sub] = ipc::channel<int>(shm_name_);

  // 持续写入
  std::thread writer([&, p = std::move(pub)]() mutable {
    for (int i = 0;; ++i) {
      p.publish(i);
      std::this_thread::sleep_for(std::chrono::microseconds(1));
    }
  });
  writer.detach(); // 后台持续运行

  // 尝试带超时的 fetch
  int val;
  bool success = sub.fetch(val, TestConfig::SHORT_TIMEOUT);

  // 应该能成功（因为持续有更新）
  EXPECT_TRUE(success);
}
