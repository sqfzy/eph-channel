#include "../fixtures/config.hpp"
#include "../fixtures/ipc_fixture.hpp"
#include "../fixtures/utils.hpp"
#include "eph/channel.hpp"
#include <cerrno>
#include <filesystem>
#include <gtest/gtest.h>
#include <sys/wait.h>

using namespace eph::ipc;
using namespace eph::test;

TEST_F(IpcTestFixture, CreateAndConnect) {
  Sender<int> sender(shm_name_);
  Receiver<int> receiver(shm_name_);

  EXPECT_EQ(sender.name(), shm_name_);
  EXPECT_EQ(receiver.name(), shm_name_);
}

TEST_F(IpcTestFixture, CrossProcessCommunication) {
  ForkedProcess fork_helper;

  if (fork_helper.fork() == ForkedProcess::Role::Child) {
    // 子进程：Receiver
    Receiver<TestMessage> receiver(shm_name_);

    auto msg = receiver.receive();

    // 验证接收到的数据
    if (msg.id == 12345 && msg.value == 3.14) {
      fork_helper.child_exit(0); // 成功
    } else {
      fork_helper.child_exit(1); // 失败
    }
  } else {
    // 父进程：Sender
    Sender<TestMessage> sender(shm_name_);

    // 等待子进程启动
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    TestMessage msg{};
    msg.id = 12345;
    msg.timestamp = 67890;
    msg.value = 3.14;

    sender.send(msg);

    // 等待子进程
    int exit_code = fork_helper.wait_child();
    EXPECT_EQ(exit_code, 0);
  }
}

TEST_F(IpcTestFixture, LargeDataTransfer) {
  TestDataGenerator gen;

  ForkedProcess fork_helper;

  if (fork_helper.fork() == ForkedProcess::Role::Child) {
    Receiver<LargeTestData> receiver(shm_name_);

    auto data = receiver.receive();

    // 验证数据完整性
    if (data.sequence == 999999) {
      fork_helper.child_exit(0);
    } else {
      fork_helper.child_exit(1);
    }
  } else {
    Sender<LargeTestData> sender(shm_name_);

    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    auto data = gen.generate_large_data(999999);
    sender.send(data);

    EXPECT_EQ(fork_helper.wait_child(), 0);
  }
}

TEST_F(IpcTestFixture, ShmCleanup) {
  std::string shm_path = "/dev/shm" + shm_name_;

  {
    Sender<int> sender(shm_name_);
    Receiver<int> receiver(shm_name_);

    sender.send(42);
    EXPECT_EQ(receiver.receive(), 42);

    // 在对象存活期间，SHM 文件应该存在
    EXPECT_TRUE(std::filesystem::exists(shm_path));
  } // sender 和 receiver 析构

  // Sender 析构后，SHM 应该被删除（因为 Sender 是 Owner）
  EXPECT_FALSE(std::filesystem::exists(shm_path));

  // 此时 shm_unlink 应该失败（因为已被删除）
  int result = shm_unlink(shm_name_.c_str());
  EXPECT_EQ(result, -1);
  EXPECT_EQ(errno, ENOENT); // No such file or directory
}

TEST_F(IpcTestFixture, CapacityOverflow) {
  auto [sender, receiver] = channel<int, 4>(shm_name_);

  // 填满
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(sender.try_send(i));
  }

  EXPECT_TRUE(sender.is_full());
  EXPECT_FALSE(sender.try_send(999));
}
