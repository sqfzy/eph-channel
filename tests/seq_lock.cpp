#include <gtest/gtest.h>
#include "eph/core/seq_lock.hpp"

#include <atomic>
#include <thread>
#include <vector>
#include <latch>
#include <chrono>
#include <cstring>
#include <print>

using namespace eph;

// =============================================================================
// 辅助数据结构
// =============================================================================

// 基础 POD 类型
struct Point {
    int x = 0;
    int y = 0;
    bool operator==(const Point& other) const = default;
};

// 带有校验和的数据结构，用于完整性测试 (检测 Tearing)
struct IntegrityData {
    int64_t id = 0;
    int64_t checksum = 0;
    
    // 填充以确保跨越多个缓存行，增加撕裂概率
    char padding[50] = {0}; 

    void set_id(int64_t new_id) {
        id = new_id;
        checksum = calculate_checksum(new_id);
    }

    [[nodiscard]] bool is_valid() const {
        return checksum == calculate_checksum(id);
    }

    static int64_t calculate_checksum(int64_t val) {
        // 简单的哈希计算
        return val * 0xCAFEBABE; 
    }
};

// 大对象测试
struct LargeStruct {
    char data[256];
    bool operator==(const LargeStruct& other) const {
        return std::memcmp(data, other.data, sizeof(data)) == 0;
    }
};

// 对齐测试
struct alignas(128) AlignedStruct {
    int val;
    bool operator==(const AlignedStruct& other) const = default;
};

// =============================================================================
// Category 1: 基础功能测试
// =============================================================================

TEST(SeqLockTest, InitialState) {
    SeqLock<Point> sl;
    
    // 初始状态不应处于忙碌状态
    EXPECT_FALSE(sl.may_busy());
    
    // 初始数据应为默认构造值
    Point p = sl.load();
    EXPECT_EQ(p.x, 0);
    EXPECT_EQ(p.y, 0);
}

TEST(SeqLockTest, BasicReadWrite) {
    SeqLock<Point> sl;
    
    // 第一次写入
    Point p1{10, 20};
    sl.store(p1);
    
    // 读取验证
    Point p2 = sl.load();
    EXPECT_EQ(p2, p1);
    
    // 第二次写入
    Point p3{30, 40};
    sl.store(p3);
    Point p4 = sl.load();
    EXPECT_EQ(p4, p3);
}

TEST(SeqLockTest, ZeroCopyApi) {
    SeqLock<Point> sl;
    sl.store({1, 1});

    // Zero-copy Write: 直接修改内部数据
    sl.write([](Point& p) {
        p.x = 100;
        p.y += 1;
    });

    // Zero-copy Read: 访问器模式
    bool success = sl.try_read([](const Point& p) {
        EXPECT_EQ(p.x, 100);
        EXPECT_EQ(p.y, 2);
    });
    EXPECT_TRUE(success);
}

// =============================================================================
// Category 2: 确定性并发测试 (Deterministic)
// 利用 std::latch 控制执行顺序，模拟特定的竞争条件
// =============================================================================

// 测试场景：Writer 正在写时，Reader 应该失败
TEST(SeqLockTest, ReaderFailsWhenWriterIsBusy) {
    SeqLock<Point> sl;
    std::latch writer_inside(1);
    std::latch reader_done(1);

    std::thread writer([&]() {
        sl.write([&](Point& p) {
            p.x = 99;
            // 1. 通知主线程：我已经持有锁（seq 为奇数）
            writer_inside.count_down(); 
            // 2. 阻塞，保持锁住状态
            reader_done.wait();         
        });
    });

    // 等待 Writer 进入临界区
    writer_inside.wait(); 

    // 断言：此时 SeqLock 应该显示忙碌
    EXPECT_TRUE(sl.may_busy());
    
    // 断言：try_load 应该失败
    Point p;
    EXPECT_FALSE(sl.try_load(p));

    // 让 Writer 完成
    reader_done.count_down(); 
    writer.join();

    // Writer 完成后应该能读到新数据
    EXPECT_TRUE(sl.try_load(p));
    EXPECT_EQ(p.x, 99);
}

// 测试场景：读取过程中发生了写入，Reader 应该检测到并重试/失败
TEST(SeqLockTest, ReaderRetriesWhenDataChanges) {
    SeqLock<Point> sl;
    std::latch reader_inside(1);
    std::latch writer_done(1);

    std::thread reader([&]() {
        // 开始读取...
        bool success = sl.try_read([&](const Point& p) {
            // 1. 通知主线程：我已经开始读了（获取了开始版本号）
            reader_inside.count_down(); 
            // 2. 等待主线程修改数据（这会改变结束版本号）
            writer_done.wait();         
            
            // 此时 p 的内存可能被改写，但我们不访问它，只让 lambda 结束
        });
        // 断言：由于版本号不一致，读取应该失败
        EXPECT_FALSE(success); 
    });

    // 等待 Reader 进入读取函数
    reader_inside.wait(); 

    // Writer 修改数据，导致 seq 增加
    sl.store({88, 88}); 
    
    // 通知 Reader 继续（执行版本号校验）
    writer_done.count_down(); 

    reader.join();
}

// =============================================================================
// Category 3: 并发压力测试 (Stress)
// 验证在高频读写下数据的一致性，确保没有 "Tearing" (撕裂读)
// =============================================================================

TEST(SeqLockTest, DataIntegrityUnderContention) {
    SeqLock<IntegrityData> sl;
    std::atomic<bool> running{true};
    
    // 1. Writer Thread: 疯狂更新 ID 和 Checksum
    std::thread writer([&]() {
        int64_t id = 0;
        while (running) {
            sl.write([&](IntegrityData& data) {
                data.set_id(++id);
            });
            // 提示：如果要测试更极端的竞争，可以加一点点随机 yield，
            // 但 SeqLock 是 Wait-free 写，全速跑更能测试 Reader 的抗压能力。
        }
    });

    // 2. Reader Threads: 疯狂读取并校验
    const int num_readers = 4;
    std::vector<std::thread> readers;
    std::atomic<int64_t> total_valid_reads{0};
    std::atomic<int64_t> failed_reads{0};

    for (int i = 0; i < num_readers; ++i) {
        readers.emplace_back([&]() {
            IntegrityData local_data;
            while (running) {
                if (sl.try_load(local_data)) {
                    // 如果读取成功，数据必须有效（ID 和 Checksum 匹配）
                    // 绝不能出现：读到了新 ID 但 Checksum 还是旧的
                    ASSERT_TRUE(local_data.is_valid()) 
                        << "Tearing detected! ID: " << local_data.id 
                        << " Checksum: " << local_data.checksum;
                    total_valid_reads++;
                } else {
                    failed_reads++;
                }
            }
        });
    }

    // 运行 1 秒
    std::this_thread::sleep_for(std::chrono::seconds(1));
    running = false;

    writer.join();
    for (auto& t : readers) t.join();

    std::print("[Stress] Readers: {}, Valid Reads: {}, Retry/Failed: {}\n", 
               num_readers, total_valid_reads.load(), failed_reads.load());
    
    // 至少应该有一些成功的读取
    EXPECT_GT(total_valid_reads.load(), 0);
}

// =============================================================================
// Category 4: 边界与类型测试
// =============================================================================

TEST(SeqLockTest, LargeStructSupport) {
    SeqLock<LargeStruct> sl;
    LargeStruct ls;
    // 填充特定模式
    std::memset(ls.data, 0xAB, sizeof(ls.data));
    
    sl.store(ls);
    LargeStruct read_back = sl.load();
    
    EXPECT_EQ(ls, read_back);
}

TEST(SeqLockTest, AlignmentSupport) {
    SeqLock<AlignedStruct> sl;
    
    AlignedStruct as{123};
    sl.store(as);
    AlignedStruct res = sl.load();
    
    EXPECT_EQ(res.val, 123);
    
    // 验证 SeqLock 本身的对齐是否满足内部元素的要求
    // 如果 T 要求 128 字节对齐，SeqLock<T> 也应该至少 128 字节对齐
    EXPECT_GE(alignof(SeqLock<AlignedStruct>), 128);
}
