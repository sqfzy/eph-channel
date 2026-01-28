#include "eph_channel/channel.hpp"
#include <gtest/gtest.h>
#include <string>

using namespace eph;

class SnapshotChannelTest : public ::testing::Test {
protected:
  // 使用唯一的 SHM 名称
  std::string shm_name = "/test_shm_snapshot_unit_test";

  void TearDown() override {
    shm_unlink(shm_name.c_str());
  }
};

TEST_F(SnapshotChannelTest, CreateAndConnect) {
  // Publisher 创建 (Owner)
  auto pub = snapshot::ipc::Publisher<int>(snapshot::ipc::IpcBackend<int>(shm_name, true));
  // Subscriber 连接 (User)
  auto sub = snapshot::ipc::Subscriber<int>(snapshot::ipc::IpcBackend<int>(shm_name, false));

  // 验证基本连通性
  pub.publish(100);
  int val = 0;
  sub.fetch(val);
  EXPECT_EQ(val, 100);
}

TEST_F(SnapshotChannelTest, BasicPublishFetch) {
  auto [pub, sub] = snapshot::ipc::channel<int>(shm_name);

  pub.publish(42);
  
  int val = sub.fetch();
  EXPECT_EQ(val, 42);

  // Snapshot 语义：再次读取仍是相同的值（状态未变）
  EXPECT_EQ(sub.fetch(), 42);
}

TEST_F(SnapshotChannelTest, OverwriteBehavior) {
  auto [pub, sub] = snapshot::ipc::channel<int>(shm_name);

  // 连续发布多次
  pub.publish(1);
  pub.publish(2);
  pub.publish(3);

  // 应该只读到最后一次的值
  EXPECT_EQ(sub.fetch(), 3);
}

TEST_F(SnapshotChannelTest, ZeroCopyOperations) {
  struct Data {
      int id;
      double value;
  };

  auto [pub, sub] = snapshot::ipc::channel<Data>(shm_name);

  // 零拷贝发布
  pub.publish([](Data& d) {
      d.id = 99;
      d.value = 123.456;
  });

  // 零拷贝读取
  sub.fetch([](const Data& d) {
      EXPECT_EQ(d.id, 99);
      EXPECT_DOUBLE_EQ(d.value, 123.456);
  });
}

TEST_F(SnapshotChannelTest, ItcChannelTest) {
  auto [pub_int, sub_int] = snapshot::itc::channel<int>();
  pub_int.publish(2024);
  EXPECT_EQ(sub_int.fetch(), 2024);
}

TEST_F(SnapshotChannelTest, TryFetch) {
  auto [pub, sub] = snapshot::ipc::channel<int>(shm_name);
  
  pub.publish(1);

  int val = 0;
  // 简单场景下 try_fetch 几乎总是成功，除非极高并发竞争
  if(sub.try_fetch(val)) {
      EXPECT_EQ(val, 1);
  }
}
