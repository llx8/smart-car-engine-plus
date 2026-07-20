#include <gtest/gtest.h>
#include <thread>
#include <atomic>
#include <cstring>
#include <chrono>
#include <cstdint>

#include "ShmLayout.hpp"

// ── 测试夹具：在堆上分配对齐内存模拟共享内存 ──

class SeqLockFixture : public ::testing::Test {
protected:
    void SetUp() override {
        constexpr size_t kTotal = sizeof(ShmHeader) + sizeof(ShmBlock) + 64;
        raw_mem_ = new char[kTotal];
        uintptr_t aligned = ((uintptr_t)raw_mem_ + 63) & ~(uintptr_t)63;
        header_ = reinterpret_cast<ShmHeader*>(aligned);
        block_  = reinterpret_cast<ShmBlock*>(aligned + sizeof(ShmHeader));
        std::memset(header_, 0, sizeof(ShmHeader));
        std::memset(block_, 0, sizeof(ShmBlock));
        header_->magic = 0xCAFE0001;
        header_->version.store(0, std::memory_order_relaxed);
    }

    void TearDown() override {
        delete[] raw_mem_;
    }

    char*       raw_mem_ = nullptr;
    ShmHeader*  header_  = nullptr;
    ShmBlock*   block_   = nullptr;
};

// ── 辅助：Seqlock 写端（单帧写入） ──
static void seqlock_write(ShmHeader* header, ShmBlock* block,
                          float spd, int rpm, float wt, float ot,
                          float fuel, float bat) {
    header->version.fetch_add(1, std::memory_order_acq_rel);   // 偶数→奇数，拿写锁
    std::atomic_thread_fence(std::memory_order_release);

    block->speed_kmh      = spd;
    block->engine_rpm     = rpm;
    block->water_temp_c   = wt;
    block->oil_temp_c     = ot;
    block->fuel_percent   = fuel;
    block->battery_voltage = bat;

    std::atomic_thread_fence(std::memory_order_release);
    header->version.fetch_add(1, std::memory_order_release);   // 奇数→偶数，放写锁
}

// ── 辅助：Seqlock 读端，返回是否读到一致数据 ──
static bool seqlock_read(const ShmHeader* header, const ShmBlock* block,
                         ShmBlock& local, int max_retry = 5) {
    for (int retry = 0; retry < max_retry; ++retry) {
        uint32_t v1 = header->version.load(std::memory_order_acquire);
        if (v1 & 1) {                     // 奇数 → 写端持有锁
            std::this_thread::yield();
            continue;
        }
        std::memcpy(&local, block, sizeof(ShmBlock));
        uint32_t v2 = header->version.load(std::memory_order_acquire);
        if (v1 == v2) return true;        // 前后版本一致 → 数据完整
    }
    return false;                          // 超过重试上限
}

// ═══════════════════════════════════════════
// 测试1：单写单读 — 读端数据内部一致
// ═══════════════════════════════════════════

TEST_F(SeqLockFixture, SingleWriterSingleReader) {
    constexpr int kIterations = 5000;

    std::atomic<bool> start{false};
    std::atomic<int>  writes_done{0};

    std::thread writer([&]() {
        while (!start.load(std::memory_order_acquire)) {}
        for (int i = 0; i < kIterations; ++i) {
            float v = static_cast<float>(i);
            seqlock_write(header_, block_,
                          v, i,            // speed = version, rpm = version
                          v + 1.0f, v + 2.0f,
                          v + 3.0f, v + 4.0f);
            writes_done.store(i + 1, std::memory_order_release);
        }
    });

    start.store(true, std::memory_order_release);

    int success = 0;
    int torn    = 0;

    while (writes_done.load(std::memory_order_acquire) < kIterations) {
        ShmBlock local;
        if (seqlock_read(header_, block_, local)) {
            // 一致性检查：speed_kmh 和 engine_rpm 来自同一帧，应该相等
            if (static_cast<int>(local.speed_kmh) != local.engine_rpm) {
                torn++;
            }
            success++;
        }
    }

    writer.join();

    EXPECT_EQ(torn, 0) << "Seqlock 未能防止撕裂读：读到 " << torn << " 次不一致数据";
    EXPECT_GT(success, kIterations / 10) << "读到的一致帧过少：" << success;
}

// ═══════════════════════════════════════════
// 测试2：写锁检测 — 奇偶版本号保护
// ═══════════════════════════════════════════

TEST_F(SeqLockFixture, WriteLockDetection) {
    // 构造一个"写端长期持有锁"的场景
    // 读端应该检测到奇数版本号并重试，而非强行读取

    std::atomic<bool> lock_held{false};
    std::atomic<bool> reader_detected{false};

    std::thread writer([&]() {
        // 拿写锁（偶数→奇数）但不释放
        header_->version.fetch_add(1, std::memory_order_acq_rel);
        lock_held.store(true, std::memory_order_release);

        std::this_thread::sleep_for(std::chrono::milliseconds(10));

        // 释放写锁（奇数→偶数）
        header_->version.fetch_add(1, std::memory_order_release);
    });

    // 等写端拿到锁
    while (!lock_held.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // 此时 version 为奇数，读端应该检测到
    uint32_t v = header_->version.load(std::memory_order_acquire);
    EXPECT_EQ(v & 1, 1u) << "写端应持有锁（version 为奇数），实际 version=" << v;

    // 等写端释放锁后再读
    writer.join();

    ShmBlock local;
    bool ok = seqlock_read(header_, block_, local, 3);
    EXPECT_TRUE(ok) << "写端释放锁后，读端应该能读到一致数据";
}

// ═══════════════════════════════════════════
// 测试3：高频写入（模拟 20Hz）— 读端不掉队
// ═══════════════════════════════════════════

TEST_F(SeqLockFixture, HighFrequencyWrite) {
    constexpr int    kDurationMs = 500;      // 跑 500ms
    constexpr int    kIntervalMs = 50;       // 50ms → 20Hz
    constexpr int    kExpectedMinWrites = (kDurationMs / kIntervalMs) - 1;  // ~9

    std::atomic<int> writes_done{0};
    std::atomic<int> reads_done{0};
    std::atomic<bool> stop{false};

    // 写线程：20Hz
    std::thread writer([&]() {
        while (!stop.load(std::memory_order_acquire)) {
            float v = static_cast<float>(writes_done.load(std::memory_order_relaxed));
            seqlock_write(header_, block_,
                          v, static_cast<int>(v),
                          80.0f, 90.0f, 65.0f, 12.4f);
            writes_done.fetch_add(1, std::memory_order_release);
            std::this_thread::sleep_for(std::chrono::milliseconds(kIntervalMs));
        }
    });

    // 读线程：不断读
    std::thread reader([&]() {
        while (!stop.load(std::memory_order_acquire)) {
            ShmBlock local;
            if (seqlock_read(header_, block_, local)) {
                reads_done.fetch_add(1, std::memory_order_relaxed);
            }
        }
    });

    std::this_thread::sleep_for(std::chrono::milliseconds(kDurationMs));
    stop.store(true, std::memory_order_release);

    writer.join();
    reader.join();

    int w = writes_done.load();
    int r = reads_done.load();

    EXPECT_GE(w, kExpectedMinWrites) << "写入次数不足：" << w;
    EXPECT_GT(r, 0) << "没有成功读到任何一帧";
    // 20Hz 场景下重试极少，读次数远超写次数是正常的
}
