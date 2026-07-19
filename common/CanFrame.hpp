#pragma once

#include <cstdint>    // uint8_t, int32_t
#include <linux/can.h> // struct can_frame

namespace can_ids {
    // 5 类传感器帧 ID
    constexpr uint32_t kSpeed        = 0x100; // 车速帧
    constexpr uint32_t kEngineRpm    = 0x101; // 发动机转速帧
    constexpr uint32_t kWaterOilTemp = 0x102; // 水温油温帧
    constexpr uint32_t kFuelBattery  = 0x103; // 燃油电池帧
    constexpr uint32_t kGearStatus   = 0x104; // 档位状态帧
}

// 车况全量数据 — 所有模块以此为统一交换格式
struct CanData {
    float    speed_kmh; // 车速 km/h
    int32_t  engine_rpm; // 发动机转速 rpm
    float    water_temp_c; // 水温 ℃
    float    oil_temp_c; // 油温 ℃
    float    fuel_percent; // 燃油百分比 %
    float    battery_voltage; // 电池电压 V
    uint8_t  gear;        // 0=P 1=R 2=N 3=D
    uint8_t  hand_brake;  // 0=off 1=on
    uint8_t  lock_status; // 0=unlock 1=lock
};

// CAN 帧编码函数（can_sim 用）

// 车速帧 (0x100): [b0..b1] uint16 大端, factor=100
struct can_frame encode_speed_frame(float speed_kmh);

// 转速帧 (0x101): [b0..b1] uint16 大端, factor=1
struct can_frame encode_rpm_frame(int32_t rpm);

// 水温油温帧 (0x102): [b0..b1] 水温 uint16(factor=10), [b2..b3] 油温 uint16(factor=10)
struct can_frame encode_water_oil_frame(float water, float oil);

// 燃油电池帧 (0x103): [b0..b1] 燃油 uint16(factor=10), [b2..b3] 电池 uint16(factor=100)
struct can_frame encode_fuel_battery_frame(float fuel, float battery);

// 档位帧 (0x104): [b0] gear, [b1] hand_brake, [b2] lock_status
struct can_frame encode_gear_frame(uint8_t gear, uint8_t hand_brake, uint8_t lock_status);

// CAN 帧解码函数（car_core 用)
// 识别 frame.can_id，解析对应字段填充到 CanData
void decode_can_frame(const struct can_frame& frame, CanData& data);
