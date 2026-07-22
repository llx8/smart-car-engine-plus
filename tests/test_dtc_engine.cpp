#include <gtest/gtest.h>
#include "DtcEngine.hpp"
#include "ShmLayout.hpp"

// ── 测试夹具：准备 DtcEngine + 模拟 ShmBlock ──
class DtcEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::memset(&data_, 0, sizeof(data_));
        engine_ = new DtcEngine;
    }

    void TearDown() override {
        delete engine_;
    }

    // 快捷注册一项故障
    void reg(uint32_t code, DtcCategory cat, uint8_t sev,
             uint8_t lamp_bit, uint8_t blink) {
        engine_->registerFault(code, cat, sev, lamp_bit, blink);
    }

    ShmBlock  data_{};
    DtcEngine* engine_ = nullptr;
};

// ──────────────────────────────────────────────
// 测试1：故障注册后初始状态无活跃故障
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, NoFaultInitially) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);

    engine_->update(data_, 0);
    EXPECT_EQ(engine_->faultLampMask(), 0u);
    EXPECT_TRUE(engine_->activeDtcs().empty());
}

// ──────────────────────────────────────────────
// 测试2：水温过高确认 — 滑动窗口内 3 次触发
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, WaterTempOverheatConfirmed) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);

    // 注入 3 次水温过高，间隔 1 秒（10s 窗口内）
    data_.water_temp_c = 125.0f;
    engine_->update(data_, 0);       // t=0
    EXPECT_EQ(engine_->faultLampMask(), 0u);  // 仅 1 次，未确认

    data_.water_temp_c = 125.0f;
    engine_->update(data_, 1000);    // t=1s
    EXPECT_EQ(engine_->faultLampMask(), 0u);  // 仅 2 次，未确认

    data_.water_temp_c = 125.0f;
    engine_->update(data_, 2000);    // t=2s，第 3 次 → 确认
    EXPECT_NE(engine_->faultLampMask(), 0u);    // bit0 置位
    EXPECT_EQ(engine_->faultLampMask(), 1u);     // lamp_bit=0
    ASSERT_EQ(engine_->activeDtcs().size(), 1u);
    EXPECT_EQ(engine_->activeDtcs()[0].code, dtc_codes::P0115);
    EXPECT_TRUE(engine_->activeDtcs()[0].active);
}

// ──────────────────────────────────────────────
// 测试3：不足 3 次异常不确认（防抖机制）
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, TwoFaultsNotConfirmed) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);

    data_.water_temp_c = 125.0f;
    engine_->update(data_, 0);
    data_.water_temp_c = 125.0f;
    engine_->update(data_, 1000);

    EXPECT_EQ(engine_->faultLampMask(), 0u);
    EXPECT_TRUE(engine_->activeDtcs().empty());
}

// ──────────────────────────────────────────────
// 测试4：异常分散超窗口长度不确认
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, AnomaliesSpreadBeyondWindowNotConfirmed) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);

    data_.water_temp_c = 125.0f;
    engine_->update(data_, 0);
    data_.water_temp_c = 125.0f;
    engine_->update(data_, 6000);    // 6s，还在窗口内
    data_.water_temp_c = 125.0f;
    engine_->update(data_, 12000);   // 12s，第一次已出窗口，只有最近 2 次

    EXPECT_EQ(engine_->faultLampMask(), 0u);
    EXPECT_TRUE(engine_->activeDtcs().empty());
}

// ──────────────────────────────────────────────
// 测试5：确认后消退 — 窗口内无水 → 故障灯灭,但仍在列表里(设计:CONFIRMED 状态保留)
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, FaultRecoveryAfterWindowEmpty) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);

    // 先确认故障
    for (int i = 0; i < 3; ++i) {
        data_.water_temp_c = 125.0f;
        engine_->update(data_, i * 1000);
    }
    ASSERT_NE(engine_->faultLampMask(), 0u);

    // 不再注入异常,等窗口过期 → 故障灯灭,但 activeDtcs 仍保留
    data_.water_temp_c = 85.0f;
    engine_->update(data_, 3000 + 11000);  // 所有 timestamps 过期

    EXPECT_EQ(engine_->faultLampMask(), 0u);          // 故障灯灭
    EXPECT_EQ(engine_->activeDtcs().size(), 1u);      // 但故障仍在已确认列表
    EXPECT_FALSE(engine_->activeDtcs()[0].active);    // 标记为当前未触发
}

// ──────────────────────────────────────────────
// 测试6：冻结帧数据 — 确认时记录快照
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, FreezeFrameCapturedOnConfirm) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);

    // 设置当时车况
    data_.water_temp_c   = 115.0f;
    data_.speed_kmh      = 60.0f;
    data_.engine_rpm     = 2500;
    data_.oil_temp_c     = 90.0f;
    data_.fuel_percent   = 55.0f;
    data_.battery_voltage = 13.2f;

    for (int i = 0; i < 3; ++i) {
        engine_->update(data_, i * 1000);
    }

    ASSERT_EQ(engine_->activeDtcs().size(), 1u);
    const auto& ff = engine_->activeDtcs()[0].freeze;
    EXPECT_FLOAT_EQ(ff.speed_kmh, 60.0f);
    EXPECT_EQ(ff.engine_rpm, 2500);
    EXPECT_FLOAT_EQ(ff.water_temp_c, 115.0f);
    EXPECT_FLOAT_EQ(ff.oil_temp_c, 90.0f);
    EXPECT_FLOAT_EQ(ff.fuel_percent, 55.0f);
    EXPECT_FLOAT_EQ(ff.battery_voltage, 13.2f);
    EXPECT_EQ(ff.timestamp_ms, 0u);  // 首次检测时记录快照，非确认时
}

// ──────────────────────────────────────────────
// 测试7：故障灯掩码多 bit 独立不互串
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, MultipleFaultsIndependent) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);  // bit0
    reg(dtc_codes::B0020, DtcCategory::B, 2, 2, 1);  // bit2

    // 触发 P0115（水温高）
    for (int i = 0; i < 3; ++i) {
        data_.water_temp_c = 125.0f;
        data_.speed_kmh     = 30.0f;   // 正常车速,不会触发 B0020
        engine_->update(data_, i * 1000);
    }
    EXPECT_EQ(engine_->faultLampMask(), (1u << 0));

    // 触发 B0020(超速近碰撞)
    for (int i = 0; i < 3; ++i) {
        data_.water_temp_c = 125.0f;   // 保持
        data_.speed_kmh     = 75.0f;   // 超速
        engine_->update(data_, 3000 + i * 1000);
    }
    EXPECT_EQ(engine_->faultLampMask(), (1u << 0) | (1u << 2));
    ASSERT_EQ(engine_->activeDtcs().size(), 2u);
}

// ──────────────────────────────────────────────
// 测试8：闪烁模式填充正确
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, BlinkModeFilled) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);  // bit0 blink=2 (慢闪)
    reg(dtc_codes::C0035, DtcCategory::C, 2, 1, 1);  // bit1 blink=1 (常亮)

    // 同时触发两个故障: P0115 水温高, C0035 高速高转速
    for (int i = 0; i < 3; ++i) {
        data_.water_temp_c = 125.0f;
        data_.speed_kmh     = 60.0f;
        data_.engine_rpm    = 4500;     // speed>50 && rpm>4000
        engine_->update(data_, i * 1000);
    }

    uint8_t blinks[8] = {};
    engine_->fillBlinkModes(blinks);
    EXPECT_EQ(blinks[0], 2);  // 水温 → 慢闪
    EXPECT_EQ(blinks[1], 1);  // ABS → 常亮
    EXPECT_EQ(blinks[2], 0);  // 未触发
}

// ──────────────────────────────────────────────
// 测试9：清除所有故障后掩码归零
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, ClearAllResetsEngine) {
    reg(dtc_codes::P0115, DtcCategory::P, 2, 0, 2);

    for (int i = 0; i < 3; ++i) {
        data_.water_temp_c = 125.0f;
        engine_->update(data_, i * 1000);
    }
    ASSERT_NE(engine_->faultLampMask(), 0u);

    engine_->clearAll();
    EXPECT_EQ(engine_->faultLampMask(), 0u);
    EXPECT_TRUE(engine_->activeDtcs().empty());

    uint8_t blinks[8] = {1, 1, 1, 1, 1, 1, 1, 1};
    engine_->fillBlinkModes(blinks);
    for (int i = 0; i < 8; ++i) EXPECT_EQ(blinks[i], 0);
}

// ──────────────────────────────────────────────
// 测试10：四种故障类型全部可同时触发
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, AllFourFaultTypesTrigger) {
    reg(dtc_codes::P0115, DtcCategory::P, 3, 0, 1);  // 发动机
    reg(dtc_codes::C0035, DtcCategory::C, 2, 1, 2);  // ABS
    reg(dtc_codes::B0020, DtcCategory::B, 3, 2, 1);  // 气囊
    reg(dtc_codes::P0560, DtcCategory::P, 2, 3, 2);  // 电池

    // 同时满足 4 个触发条件
    for (int i = 0; i < 3; ++i) {
        data_.water_temp_c    = 118.0f;   // P0115: water>110
        data_.speed_kmh       = 75.0f;    // B0020: speed>70 (也满足 C0035 的 speed>50)
        data_.engine_rpm      = 4500;     // C0035: rpm>4000
        data_.battery_voltage = 9.5f;     // P0560: batt<11
        engine_->update(data_, i * 1000);
    }

    // 4 个故障 bit0..3 全置位
    EXPECT_EQ(engine_->faultLampMask(), 0b1111u);
    EXPECT_EQ(engine_->activeDtcs().size(), 4u);
}

// ──────────────────────────────────────────────
// 测试11：电池电压低 P0560 触发与消退
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, LowBatteryTriggerAndRecovery) {
    reg(dtc_codes::P0560, DtcCategory::P, 2, 3, 2);

    // 触发: 电压低于 11V
    for (int i = 0; i < 3; ++i) {
        data_.battery_voltage = 9.5f;
        engine_->update(data_, i * 1000);
    }
    EXPECT_NE(engine_->faultLampMask(), 0u);
    ASSERT_EQ(engine_->activeDtcs().size(), 1u);
    EXPECT_EQ(engine_->activeDtcs()[0].code, dtc_codes::P0560);

    // 恢复正常电压
    for (int i = 0; i < 3; ++i) {
        data_.battery_voltage = 12.4f;
        engine_->update(data_, 3000 + i * 1000);
    }
    // 窗口内异常已过期 → 消退
    engine_->update(data_, 3000 + 11000);
    EXPECT_EQ(engine_->faultLampMask(), 0u);
}

// ──────────────────────────────────────────────
// 测试12：驾驶循环自动清除 — 连续 3 个无故障循环 → 完全清除
// ──────────────────────────────────────────────
TEST_F(DtcEngineTest, DrivingCycleAutoClear) {
    reg(dtc_codes::P0115, DtcCategory::P, 3, 0, 2);

    // 触发并确认故障
    for (int i = 0; i < 3; ++i) {
        data_.water_temp_c = 118.0f;
        engine_->update(data_, i * 1000);
    }
    ASSERT_EQ(engine_->activeDtcs().size(), 1u);

    // 故障消退: 跳到 25 秒,所有 timestamps (t=0,1,2s) 都出 10s 窗口
    data_.water_temp_c = 85.0f;
    engine_->update(data_, 25000);
    EXPECT_EQ(engine_->activeDtcs().size(), 1u);   // 消退后仍保留在列表
    EXPECT_FALSE(engine_->activeDtcs()[0].active);
    EXPECT_EQ(engine_->faultLampMask(), 0u);         // 但故障灯已灭

    // markDrivingCycle 1 次: fault_free_cycles=1,还不清除
    engine_->markDrivingCycle();
    EXPECT_EQ(engine_->activeDtcs().size(), 1u);
    // markDrivingCycle 2 次: fault_free_cycles=2,还不清除
    engine_->markDrivingCycle();
    EXPECT_EQ(engine_->activeDtcs().size(), 1u);
    // markDrivingCycle 3 次: fault_free_cycles=3,kClearCycles=3 → 完全清除
    engine_->markDrivingCycle();
    EXPECT_EQ(engine_->activeDtcs().size(), 0u);   // 驾驶循环清除后从列表消失

    // 再次触发同样故障: 应被视为新故障(全新确认流程)
    data_.water_temp_c = 118.0f;
    for (int i = 0; i < 3; ++i) {
        engine_->update(data_, 10000 + i * 1000);
    }
    // 重新确认,无复发旧状态冲突
    EXPECT_EQ(engine_->activeDtcs().size(), 1u);
    EXPECT_EQ(engine_->activeDtcs()[0].code, dtc_codes::P0115);
}
