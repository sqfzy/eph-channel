#include <gtest/gtest.h>
#include <iostream>

int main(int argc, char** argv) {
  std::cout << "========================================" << std::endl;
  std::cout << "  EPH Channel Test Suite" << std::endl;
  std::cout << "========================================" << std::endl;
  
  ::testing::InitGoogleTest(&argc, argv);
  
  // 设置测试超时（可选）
  // ::testing::GTEST_FLAG(timeout) = 3000; // 3 秒
  
  int result = RUN_ALL_TESTS();
  
  std::cout << "========================================" << std::endl;
  std::cout << (result == 0 ? "✅ All tests passed!" : "❌ Some tests failed!") << std::endl;
  std::cout << "========================================" << std::endl;
  
  return result;
}
