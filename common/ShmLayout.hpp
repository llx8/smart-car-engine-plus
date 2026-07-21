#pragma once

#include <cstdint>
#include <atomic>
#include <cstring>

// ── ShmHeader：放在共享内存开头 ──
// 必须自然对齐，严禁 #pragma pack(1) （ARM 上 atomic 对齐会崩）
struct ShmHeader {
    uint64_t                magic;       // 固定 0xCAFE_0001（只读验证）
    std::atomic<uint32_t>   version;     // 单调递增，供活跃性检测
    std::atomic<uint32_t>   read_index;  // Ping-Pong: 读端该读哪个 buf (0 或 1)
    uint32_t                _pad[4];     // 补齐到 32 字节
};
static_assert(sizeof(ShmHeader) == 32, "ShmHeader must be 32 bytes");
static_assert(alignof(ShmHeader) <= 8, "ShmHeader align <= 8");
static_assert(std::atomic<uint32_t>::is_always_lock_free, "atomic<uint32_t> must be lock-free");

// ── ShmBlock：车况数据块 ──
struct ShmBlock {
    float    speed_kmh;
    int32_t  engine_rpm;
    float    water_temp_c;
    float    oil_temp_c;
    float    fuel_percent;
    float    battery_voltage;
    uint8_t  gear, hand_brake, lock_status;
    uint8_t  door_mask;
    uint32_t fault_lamp_mask;
    uint8_t  fault_blink[8];
    int32_t  ai_status;
    char     ai_message[64];
    uint8_t  _reserved[56];
};

// ── Ping-Pong 双缓冲布局 ──
// 共享内存布局: [ShmHeader] [ShmBlock buf[0]] [ShmBlock buf[1]]
//
// 写端协议（car_core 主线程）:
//   1. target = 1 - read_index  → 写非活跃缓冲区
//   2. 更新 buf[target] 全部字段
//   3. std::atomic_thread_fence(release)  → 保证写入全局可见
//   4. read_index.store(target, release)  → 原子切换到新缓冲区
//   5. write(eventfd) 通知读端
//
// 读端协议（car_dashboard ShmReader）:
//   1. read(eventfd) 消费通知
//   2. idx = read_index.load(acquire)
//   3. memcpy(&local, buf[idx], sizeof(ShmBlock))
//   4. recheck: read_index 是否仍 == idx ?
//      是 → 数据一致（写端未在拷贝期间切换）
//      否 → 重试（最多 3 次）
//   5. 基于 local 渲染
//
// 高负载优势：写端每次只改非活跃 buf，读端无锁无版本比较，
// 常见情况下 recheck 一次通过（50ms 写周期内只切换一次），
// 较 Seqlock 减少了每次读取的 retry 开销和版本号竞争。

// ── SPSC 队列 ──
template<typename T, size_t N>
class SPSCQueue {
    T data_[N];
    std::atomic<size_t> write_idx_{0};
    char pad_[64];
    std::atomic<size_t> read_idx_{0};

public:
    // 生产者调用（CAN 线程）
    bool push(const T& item) {
        size_t w = write_idx_.load(std::memory_order_relaxed);
        size_t r = read_idx_.load(std::memory_order_acquire);
        if (w - r >= N) return false;       // 满 → 丢弃
        data_[w % N] = item;
        write_idx_.store(w + 1, std::memory_order_release);
        return true;
    }

    // 消费者调用（主线程）
    bool pop(T& item) {
        size_t r = read_idx_.load(std::memory_order_relaxed);
        size_t w = write_idx_.load(std::memory_order_acquire);
        if (r == w) return false;           // 空
        item = data_[r % N];
        read_idx_.store(r + 1, std::memory_order_release);
        return true;
    }
};
