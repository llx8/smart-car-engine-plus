#include <gtest/gtest.h>
#include "ShmLayout.hpp"

// ── 结构体大小检查 ──

TEST(ShmLayoutTest, ShmHeaderSize) {
    // ShmHeader: magic(8) + version(4) + read_index(4) + _pad[4](16) = 32
    EXPECT_EQ(sizeof(ShmHeader), 32);
}

TEST(ShmLayoutTest, ShmBlockSize) {
    // ShmBlock: 手动算一下各字段总和，确保预留合理
    // speed(4) + rpm(4) + water(4) + oil(4) + fuel(4) + battery(4) = 24
    // gear+hb+lock(3) + door_mask(1) = 4
    // fault_lamp_mask(4) + fault_blink(8) = 12
    // ai_status(4) + ai_message(64) = 68
    // _reserved(56)
    // 总计 ≈ 24+4+12+68+56 = 164
    // 期待至少 164 字节
    EXPECT_GE(sizeof(ShmBlock), 164);
}

// ── SPSC 队列测试 ──

TEST(ShmLayoutTest, SPSCPushPop) {
    SPSCQueue<int, 4> q;

    int val;
    EXPECT_FALSE(q.pop(val));  // 空队列 pop 应该返回 false

    EXPECT_TRUE(q.push(10));
    EXPECT_TRUE(q.push(20));
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 10);
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 20);
    EXPECT_FALSE(q.pop(val));  // 又空了
}

TEST(ShmLayoutTest, SPSCFullDiscard) {
    SPSCQueue<int, 3> q;

    EXPECT_TRUE(q.push(1));
    EXPECT_TRUE(q.push(2));
    EXPECT_TRUE(q.push(3));
    EXPECT_FALSE(q.push(4));  // 满队列 push 应该返回 false（丢弃）

    int val;
    EXPECT_TRUE(q.pop(val));
    EXPECT_EQ(val, 1);
}

TEST(ShmLayoutTest, SPSCWrapAround) {
    SPSCQueue<int, 3> q;

    // 绕一圈，验证环形缓冲区正确
    for (int i = 0; i < 100; i++) {
        EXPECT_TRUE(q.push(i));
        int val;
        EXPECT_TRUE(q.pop(val));
        EXPECT_EQ(val, i);
    }
}
