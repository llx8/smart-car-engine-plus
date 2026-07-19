#include "CanFrame.hpp"
#include <cmath>

// 内部工具函数：uint16 ↔ 大端字节数组（2 字节）
static void uint16_to_big_endian(uint16_t val, uint8_t* buf) {
    buf[0] = (val >> 8) & 0xFF;
    buf[1] = val & 0xFF;
}

static uint16_t big_endian_to_uint16(const uint8_t* buf) {
    return static_cast<uint16_t>((static_cast<uint16_t>(buf[0]) << 8) | buf[1]);
}

void decode_can_frame(const struct can_frame& frame, CanData& data){
    switch (frame.can_id) {
        case can_ids::kSpeed:
            // 车速: raw × 0.01 → km/h
            data.speed_kmh = big_endian_to_uint16(frame.data) * 0.01f;
            break;
        case can_ids::kEngineRpm:
            // 转速: raw × 1 → rpm
            data.engine_rpm = big_endian_to_uint16(frame.data);
            break;
        case can_ids::kWaterOilTemp:
            // 水温/油温: raw × 0.1 → °C
            data.water_temp_c = big_endian_to_uint16(frame.data) * 0.1f;
            data.oil_temp_c   = big_endian_to_uint16(frame.data + 2) * 0.1f;
            break;
        case can_ids::kFuelBattery:
            // 燃油: raw × 0.1 → %，电池电压: raw × 0.01 → V
            data.fuel_percent    = big_endian_to_uint16(frame.data) * 0.1f;
            data.battery_voltage = big_endian_to_uint16(frame.data + 2) * 0.01f;
            break;
        case can_ids::kGearStatus:
            data.gear        = frame.data[0];
            data.hand_brake  = frame.data[1];
            data.lock_status = frame.data[2];
            break;
        default:
            // 未知帧 ID，忽略
            break;
    }
}

// ── 编码函数 ──

// 车速帧 (0x100): [b0..b1] uint16 大端, factor=100
//   物理量 → raw: raw = round(speed_kmh * 100)
struct can_frame encode_speed_frame(float speed_kmh){
    struct can_frame frame{};
    frame.can_id   = can_ids::kSpeed;
    frame.can_dlc  = 2;
    uint16_to_big_endian(static_cast<uint16_t>(std::round(speed_kmh * 100.0f)), frame.data);
    return frame;
}

// 转速帧 (0x101): [b0..b1] uint16 大端, factor=1
struct can_frame encode_rpm_frame(int32_t rpm){
    struct can_frame frame{};
    frame.can_id   = can_ids::kEngineRpm;
    frame.can_dlc  = 2;
    uint16_to_big_endian(static_cast<uint16_t>(rpm), frame.data);
    return frame;
}

// 水温油温帧 (0x102): [b0..b1] 水温 uint16, [b2..b3] 油温 uint16, factor=10
struct can_frame encode_water_oil_frame(float water, float oil){
    struct can_frame frame{};
    frame.can_id   = can_ids::kWaterOilTemp;
    frame.can_dlc  = 4;
    uint16_to_big_endian(static_cast<uint16_t>(std::round(water * 10.0f)), frame.data);
    uint16_to_big_endian(static_cast<uint16_t>(std::round(oil   * 10.0f)), frame.data + 2);
    return frame;
}

// 燃油电池帧 (0x103): [b0..b1] 燃油 uint16(factor=10), [b2..b3] 电池 uint16(factor=100)
struct can_frame encode_fuel_battery_frame(float fuel, float battery){
    struct can_frame frame{};
    frame.can_id   = can_ids::kFuelBattery;
    frame.can_dlc  = 4;
    uint16_to_big_endian(static_cast<uint16_t>(std::round(fuel   * 10.0f)),  frame.data);
    uint16_to_big_endian(static_cast<uint16_t>(std::round(battery * 100.0f)), frame.data + 2);
    return frame;
}

// 档位帧 (0x104): [b0] gear, [b1] hand_brake, [b2] lock_status
struct can_frame encode_gear_frame(uint8_t gear, uint8_t hand_brake, uint8_t lock_status){
    struct can_frame frame{};
    frame.can_id   = can_ids::kGearStatus;
    frame.can_dlc  = 3;
    frame.data[0]  = gear;
    frame.data[1]  = hand_brake;
    frame.data[2]  = lock_status;
    return frame;
}