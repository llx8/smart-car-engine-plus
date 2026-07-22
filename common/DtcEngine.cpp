#include "DtcEngine.hpp"
#include <ShmLayout.hpp>

void DtcEngine::registerFault(uint32_t code, DtcCategory cat, uint8_t severity,
                              uint8_t lamp_bit, uint8_t blink_mode)
{
    Monitor m;
    m.record.code        = code;
    m.record.category    = cat;
    m.record.severity    = severity;
    m.record.active      = false;
    m.lamp_bit           = lamp_bit;
    m.blink_mode         = blink_mode;
    monitors_.push_back(m);
}

void DtcEngine::update(const ShmBlock& data, uint64_t now_ms)
{
    fault_lamp_mask_ = 0;
    std::memset(blink_modes_, 0, sizeof(blink_modes_));
    confirmed_.clear();

    for (auto& m : monitors_) {
        // ── 根据故障码判断触发条件 ──
        // 演示用直观映射: 每个故障对一个物理量,can_sim 注入时用户能直接对应
        bool fault_now = false;
        switch (m.record.code) {
        case dtc_codes::P0115:
            fault_now = (data.water_temp_c > 110.0f);
            break;
        case dtc_codes::C0035:
            // 轮速异常: 高速 + 高转速 → 轮速传感器信号不一致
            fault_now = (data.speed_kmh > 50.0f && data.engine_rpm > 4000);
            break;
        case dtc_codes::B0020:
            // 碰撞近似: 超速场景(演示无碰撞传感器,以高速近似)
            fault_now = (data.speed_kmh > 70.0f);
            break;
        case dtc_codes::P0560:
            fault_now = (data.battery_voltage < 11.0f && data.battery_voltage > 0.1f);
            break;
        default:
            continue;
        }

        if (fault_now) {
            // ── 首次检测（0→1）：记录冻结帧 ──
            if (!m.first_detected) {
                m.first_detected = true;
                m.record.first_seen_ms = now_ms;
                m.record.freeze.dtc_code       = m.record.code;
                m.record.freeze.speed_kmh      = data.speed_kmh;
                m.record.freeze.engine_rpm     = data.engine_rpm;
                m.record.freeze.water_temp_c   = data.water_temp_c;
                m.record.freeze.oil_temp_c     = data.oil_temp_c;
                m.record.freeze.fuel_percent   = data.fuel_percent;
                m.record.freeze.battery_voltage = data.battery_voltage;
                m.record.freeze.timestamp_ms   = now_ms;
            }

            // 故障复发 → 重置无故障循环计数
            if (m.was_confirmed && m.fault_free_cycles > 0) {
                m.fault_free_cycles = 0;
            }

            // 故障当前正在触发 → active=true (反映"当前是否仍在触发")
            m.record.active = true;
            m.timestamps.push_back(now_ms);

            while (!m.timestamps.empty() &&
                   now_ms - m.timestamps.front() > kWindowMs) {
                m.timestamps.pop_front();
            }

            if (!m.confirmed && m.timestamps.size() >= static_cast<size_t>(kConfirmCount)) {
                m.confirmed = true;
                m.was_confirmed = true;
                m.fault_free_cycles = 0;
                m.record.confirmed_ms = now_ms;
            }
        } else {
            // 异常消退，重置首次检测标志（下次复发重新记录冻结帧）
            m.first_detected = false;
            while (!m.timestamps.empty() &&
                   now_ms - m.timestamps.front() > kWindowMs) {
                m.timestamps.pop_front();
            }
            // 窗口归零 → 故障当前不再触发,但仍保留为已确认历史(等待 markDrivingCycle 清除)
            if (m.timestamps.empty()) {
                m.record.active = false;
            }
        }

        // 故障灯:仅在 已确认 + 当前仍在触发 时点亮 (消退即灭灯,符合真实车 MIL 行为)
        if (m.confirmed && m.record.active) {
            fault_lamp_mask_ |= (1u << m.lamp_bit);
            blink_modes_[m.lamp_bit] = m.blink_mode;
        }
        // activeDtcs: 所有已确认但未被驾驶循环清除的故障 (含已消退的)
        if (m.confirmed) {
            confirmed_.push_back(m.record);
        }
    }
}

void DtcEngine::markDrivingCycle()
{
    for (auto& m : monitors_) {
        // 跳过:从未确认过 / 当前仍在触发
        if (!m.was_confirmed || m.record.active)
            continue;

        // 已确认且当前已消退 → 累计无故障循环
        m.fault_free_cycles++;
        if (m.fault_free_cycles >= kClearCycles) {
            // 达到清除门槛,完全清除此故障 (下次复发将走全新确认流程)
            m.confirmed = false;
            m.was_confirmed = false;
            m.first_detected = false;
            m.fault_free_cycles = 0;
            m.record.active = false;
        }
    }

    // 同步刷新 confirmed_ (activeDtcs 反映清除后的状态,无需等下次 update)
    confirmed_.clear();
    for (const auto& m : monitors_) {
        if (m.confirmed) confirmed_.push_back(m.record);
    }
}

void DtcEngine::fillBlinkModes(uint8_t out[8]) const {
    std::memcpy(out, blink_modes_, 8);
}

void DtcEngine::clearAll() {
    for (auto& m : monitors_) {
        m.timestamps.clear();
        m.confirmed = false;
        m.was_confirmed = false;
        m.first_detected = false;
        m.fault_free_cycles = 0;
        m.record.active = false;
    }
    confirmed_.clear();
    fault_lamp_mask_ = 0;
    std::memset(blink_modes_, 0, sizeof(blink_modes_));
}
