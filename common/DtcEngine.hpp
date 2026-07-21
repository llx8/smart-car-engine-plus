#pragma once

#include <cstdint>
#include <vector>
#include <deque>
#include <string>
#include <cstring>

struct ShmBlock;

// ── 冻结帧：故障首次确认时的车辆状态快照 ──
struct FreezeFrame {
    uint32_t dtc_code;
    float    speed_kmh;
    int32_t  engine_rpm;
    float    water_temp_c;
    float    oil_temp_c;
    float    fuel_percent;
    float    battery_voltage;
    uint64_t timestamp_ms;
};

// ── DTC 故障码（参照 ISO 15031-6） ──
// 格式: 类别(1B P/C/B/U) + 系统(1B) + 子系统(1B) + 编号(2B) → uint32_t 编码

enum class DtcCategory : uint8_t { P = 0, C = 1, B = 2, U = 3 };

struct DtcRecord {
    uint32_t code;           // 编码后的故障码，如 0x503301 → P0301
    DtcCategory category;
    uint8_t severity;        // 1=info 2=warning 3=critical
    uint64_t first_seen_ms;  // 首次检测时间戳
    uint64_t confirmed_ms;   // 确认时间戳
    bool active = true;      // 是否当前活跃
    FreezeFrame freeze;      // 冻结帧数据

    // 辅助：转为可读字符串
    std::string toString() const {
        char buf[8]{};
        buf[0] = "PCBU"[static_cast<int>(category)];
        snprintf(buf + 1, 7, "%04X", code & 0xFFFF);
        return std::string(buf);
    }
};

// 预定义故障码
// 编码方案: 高字节 ASCII 字符('P'0x50/'C'0x43/'B'0x42/'U'0x55),
//           低 16 位为故障数字编号 → toString 可直接显示 "P0115" 等
//           必须用 8 位 hex (0xZZ_ZZZZZZ) 才能让 >>24 取到 ASCII 字符
namespace dtc_codes {
    constexpr uint32_t P0115 = 0x50000115; // 水温过高 → 发动机灯
    constexpr uint32_t C0035 = 0x43000035; // 轮速异常 → ABS灯
    constexpr uint32_t B0020 = 0x42000020; // 碰撞触发 → 气囊灯
    constexpr uint32_t P0560 = 0x50000560; // 系统电压低 → 电池灯
}

// ── DTC 诊断引擎：滑动窗口确认机制 ──

class DtcEngine {
public:
    // 注册一个监控项
    void registerFault(uint32_t code, DtcCategory cat, uint8_t severity,
                       uint8_t lamp_bit, uint8_t blink_mode);

    // 每轮主循环调用：传入当前传感器数据 + 时间戳
    void update(const struct ShmBlock& data, uint64_t now_ms);

    // 获取当前故障灯掩码（写入 ShmBlock::fault_lamp_mask）
    uint32_t faultLampMask() const { return fault_lamp_mask_; }

    // 获取当前闪烁模式（写入 ShmBlock::fault_blink）
    void fillBlinkModes(uint8_t out[8]) const;

    // 获取活跃故障列表（供 Unix socket 查询）
    const std::vector<DtcRecord>& activeDtcs() const { return confirmed_; }

    // 驾驶循环自动清除：通知引擎一个驾驶循环结束
    // 对每个已恢复的故障，累计无故障循环数。连续 3 个循环无复发 → 自动清除
    void markDrivingCycle();

    // 手动清除所有故障
    void clearAll();

private:
    static constexpr uint64_t kWindowMs = 10000;   // 10 秒滑动窗口
    static constexpr int kConfirmCount  = 3;        // 窗口内 3 次确认
    static constexpr int kClearCycles   = 3;        // 连续无故障循环数后自动清除

    struct Monitor {
        DtcRecord record;
        uint8_t lamp_bit;       // fault_lamp_mask 的 bit 位
        uint8_t blink_mode;     // 确认后的闪烁模式
        std::deque<uint64_t> timestamps;
        bool confirmed = false;
        bool was_confirmed = false;   // 曾被确认过（用于驾驶循环跟踪）
        uint8_t fault_free_cycles = 0; // 连续无故障循环计数
        bool first_detected = false;   // 首次检测（0→1），用于记录冻结帧
    };

    std::vector<Monitor> monitors_;
    std::vector<DtcRecord> confirmed_;
    uint32_t fault_lamp_mask_ = 0;
    uint8_t blink_modes_[8] = {0};
};
