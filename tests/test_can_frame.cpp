#include <gtest/gtest.h>
#include "CanFrame.hpp"

// ── 编解码往返测试 ──

TEST(CanFrameTest, SpeedRoundTrip) {
    float input = 72.5f;
    auto frame = encode_speed_frame(input);
    CanData data{};
    decode_can_frame(frame, data);
    EXPECT_FLOAT_EQ(input, data.speed_kmh);
}

TEST(CanFrameTest, RpmRoundTrip) {
    int32_t input = 2100;
    auto frame = encode_rpm_frame(input);
    CanData data{};
    decode_can_frame(frame, data);
    EXPECT_EQ(input, data.engine_rpm);
}

TEST(CanFrameTest, WaterOilRoundTrip) {
    float w = 85.3f, o = 92.7f;
    auto frame = encode_water_oil_frame(w, o);
    CanData data{};
    decode_can_frame(frame, data);
    EXPECT_FLOAT_EQ(w, data.water_temp_c);
    EXPECT_FLOAT_EQ(o, data.oil_temp_c);
}

TEST(CanFrameTest, FuelBatteryRoundTrip) {
    float f = 65.0f, b = 12.4f;
    auto frame = encode_fuel_battery_frame(f, b);
    CanData data{};
    decode_can_frame(frame, data);
    EXPECT_FLOAT_EQ(f, data.fuel_percent);
    EXPECT_FLOAT_EQ(b, data.battery_voltage);
}

TEST(CanFrameTest, GearStatusRoundTrip) {
    uint8_t gear = 3, hand_brake = 1, lock = 0;
    auto frame = encode_gear_frame(gear, hand_brake, lock);
    CanData data{};
    decode_can_frame(frame, data);
    EXPECT_EQ(gear, data.gear);
    EXPECT_EQ(hand_brake, data.hand_brake);
    EXPECT_EQ(lock, data.lock_status);
}

// ── 边界值测试 ──

TEST(CanFrameTest, SpeedBoundaries) {
    // 车速 0 km/h
    {
        auto frame = encode_speed_frame(0.0f);
        CanData data{};
        decode_can_frame(frame, data);
        EXPECT_FLOAT_EQ(0.0f, data.speed_kmh);
    }
    // 车速 260 km/h（最大值）
    {
        auto frame = encode_speed_frame(260.0f);
        CanData data{};
        decode_can_frame(frame, data);
        EXPECT_FLOAT_EQ(260.0f, data.speed_kmh);
    }
}

TEST(CanFrameTest, RpmBoundaries) {
    // 转速 0 rpm
    {
        auto frame = encode_rpm_frame(0);
        CanData data{};
        decode_can_frame(frame, data);
        EXPECT_EQ(0, data.engine_rpm);
    }
    // 转速 8000 rpm（红线区）
    {
        auto frame = encode_rpm_frame(8000);
        CanData data{};
        decode_can_frame(frame, data);
        EXPECT_EQ(8000, data.engine_rpm);
    }
}

// ── 未知帧 ID 不修改数据 ──

TEST(CanFrameTest, UnknownIdDoesNotModifyData) {
    CanData original{};
    original.speed_kmh = 50.0f;
    original.engine_rpm = 1500;
    original.water_temp_c = 80.0f;

    // 模拟一个未知 ID 的 CAN 帧
    struct can_frame frame{};
    frame.can_id = 0x999;
    frame.can_dlc = 8;

    CanData data = original;
    decode_can_frame(frame, data);

    // 检查数据没有被修改
    EXPECT_FLOAT_EQ(original.speed_kmh, data.speed_kmh);
    EXPECT_EQ(original.engine_rpm, data.engine_rpm);
    EXPECT_FLOAT_EQ(original.water_temp_c, data.water_temp_c);
}
