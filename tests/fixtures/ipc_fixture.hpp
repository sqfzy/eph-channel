#pragma once

#include "config.hpp"
#include "utils.hpp"
#include <gtest/gtest.h>

namespace eph::test {

// IPC 测试基类
class IpcTestFixture : public ::testing::Test {
protected:
  std::string shm_name_;
  std::unique_ptr<ShmCleaner> cleaner_;

  void SetUp() override {
    shm_name_ = generate_unique_shm_name();
    cleaner_ = std::make_unique<ShmCleaner>(shm_name_);
  }

  void TearDown() override {
    // 强制清理，防止测试失败残留
    cleaner_.reset();
    shm_unlink(shm_name_.c_str());
  }

  const std::string& get_shm_name() const { return shm_name_; }
};

// 参数化测试：不同容量大小
class ParameterizedCapacityTest : public IpcTestFixture,
                                   public ::testing::WithParamInterface<size_t> {
protected:
  size_t get_capacity() const { return GetParam(); }
};

} // namespace eph::test
