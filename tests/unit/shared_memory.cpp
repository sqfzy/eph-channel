#include "eph/core/shared_memory.hpp"
#include "../fixtures/config.hpp"
#include "../fixtures/utils.hpp"
#include "../fixtures/ipc_fixture.hpp"
#include <gtest/gtest.h>
#include <sys/mman.h>
#include <sys/wait.h>

using namespace eph;
using namespace eph::test;


TEST_F(IpcTestFixture, CreateSharedMemory) {
  SharedMemory<TestMessage> shm(shm_name_, true); // is_owner = true
  
  EXPECT_NE(shm.data(), nullptr);
  
  // 验证可以写入数据
  shm->id = 123;
  shm->timestamp = 456;
  shm->value = 7.89;
  
  EXPECT_EQ(shm->id, 123);
}

TEST_F(IpcTestFixture, ConnectSharedMemory) {
  // Owner 创建
  SharedMemory<int> shm_owner(shm_name_, true);
  
  *shm_owner.data() = 999;
  
  // User 连接
  SharedMemory<int> shm_user(shm_name_, false);
  
  // 验证可以读取相同数据
  EXPECT_EQ(*shm_user.data(), 999);
  
  // User 修改
  *shm_user.data() = 777;
  
  // Owner 应该看到修改
  EXPECT_EQ(*shm_owner.data(), 777);
}

TEST_F(IpcTestFixture, NameCollision) {
  SharedMemory<int> shm1(shm_name_, true);
  
  *shm1.data() = 42;
  
  // User 连接同名 SHM（不是重复创建）
  SharedMemory<int> shm2(shm_name_, false);
  
  // 应该指向相同的内存
  EXPECT_EQ(*shm2.data(), 42);
}

TEST_F(IpcTestFixture, AutoCleanup) {
  {
    SharedMemory<int> shm(shm_name_, true);
    *shm.data() = 123;
  } // shm 析构
  
  // 尝试连接（应该失败或找不到）
  EXPECT_THROW({
    SharedMemory<int> shm(shm_name_, false);
  }, std::exception);
}


TEST_F(IpcTestFixture, CrossProcessVisibility) {
  ForkedProcess fork_helper;
  
  if (fork_helper.fork() == ForkedProcess::Role::Child) {
    // 子进程：等待父进程写入
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    SharedMemory<TestMessage> shm(shm_name_, false);
    
    // 验证能读取到父进程写入的数据
    if (shm->id == 12345 && shm->value == 3.14159) {
      fork_helper.child_exit(0);
    } else {
      fork_helper.child_exit(2);
    }
  } else {
    // 父进程：创建并写入数据
    SharedMemory<TestMessage> shm(shm_name_, true);
    
    shm->id = 12345;
    shm->timestamp = 67890;
    shm->value = 3.14159;
    
    int exit_code = fork_helper.wait_child();
    EXPECT_EQ(exit_code, 0);
  }
}


TEST_F(IpcTestFixture, SizeLimits) {
  // 测试非常小的结构
  struct TinyStruct { char c; };
  SharedMemory<TinyStruct> shm_tiny(shm_name_ + "_tiny", true);
  shm_tiny->c = 'X';
  EXPECT_EQ(shm_tiny->c, 'X');
  
  // 测试较大的结构（但不超过 2GB）
  struct MediumStruct { char data[1024 * 1024]; }; // 1MB
  
  std::string medium_name = shm_name_ + "_medium";
  ShmCleaner medium_cleaner(medium_name);
  
  SharedMemory<MediumStruct> shm_medium(medium_name, true);
  shm_medium->data[0] = 'X';
  EXPECT_EQ(shm_medium->data[0], 'X');
}


TEST_F(IpcTestFixture, OpenNonExistentShm) {
  EXPECT_THROW({
    SharedMemory<int> shm("/nonexistent_shm_12345", false);
  }, std::exception);
}


TEST_F(IpcTestFixture, ConcurrentAccess) {
  struct SharedCounter {
    alignas(64) std::atomic<int> value{0};
  };
  
  SharedMemory<SharedCounter> shm(shm_name_, true);
  
  ThreadRunner threads;
  constexpr int NUM_THREADS = 4;
  constexpr int INCREMENTS = 10000;
  
  for (int i = 0; i < NUM_THREADS; ++i) {
    threads.spawn([&]() {
      for (int j = 0; j < INCREMENTS; ++j) {
        shm->value.fetch_add(1, std::memory_order_relaxed);
      }
    });
  }
  
  threads.join_all();
  
  EXPECT_EQ(shm->value.load(), NUM_THREADS * INCREMENTS);
}


TEST_F(IpcTestFixture, MemoryAlignment) {
  struct AlignedStruct {
    alignas(64) int value;
  };
  
  SharedMemory<AlignedStruct> shm(shm_name_, true);
  
  verify_alignment(shm.data(), 64);
}


TEST_F(IpcTestFixture, MoveSemantics) {
  SharedMemory<int> shm1(shm_name_, true);
  *shm1.data() = 42;
  
  // 移动构造
  SharedMemory<int> shm2(std::move(shm1));
  EXPECT_EQ(*shm2.data(), 42);
  
  // shm1 已失效
  EXPECT_EQ(shm1.data(), nullptr);
}

TEST_F(IpcTestFixture, MoveAssignment) {
  SharedMemory<int> shm1(shm_name_, true);
  *shm1.data() = 42;
  
  std::string another_name = shm_name_ + "_another";
  ShmCleaner another_cleaner(another_name);
  SharedMemory<int> shm2(another_name, true);
  *shm2.data() = 99;
  
  // 移动赋值
  shm2 = std::move(shm1);
  EXPECT_EQ(*shm2.data(), 42);
}
