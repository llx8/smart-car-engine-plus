#pragma once

#include <cstdint>
#include <atomic>
#include <cstring>

// ── ShmHeader：放在共享内存开头 ──
// 必须自然对齐，严禁 #pragma pack(1) （ARM 上 atomic 对齐会崩）
struct ShmHeader {
    uint64_t                magic;      // 固定 0xCAFE_CAFE_CAFE_CAFE
    std::atomic<uint32_t>   version;    // Seqlock 版本号
    uint32_t _pad[5];    // 补齐到 32 字节
};

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
