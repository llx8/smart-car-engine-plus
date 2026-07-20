#pragma once

#include <cstdint>

// ── 通信协议常量 ──

constexpr uint16_t kCarMsgMagic = 0x4341; // "CA"

enum class CmdType : uint8_t {
    READ    = 0x01,
    WRITE   = 0x02,
    GET_ALL = 0x03
};

enum class ModId : uint8_t {
    DOOR = 1,
    AC   = 2
};

enum class ResultCode : uint8_t {
    OK           = 0x00,
    ERR          = 0x01,
    UNKNOWN_CMD  = 0x02,
    UNKNOWN_MOD  = 0x03,
    UNKNOWN_ITEM = 0x04,
    OUT_OF_RANGE = 0x05
};

// ── 门控字段索引 ──
namespace door_item {
    constexpr uint8_t kWindowFL = 0; // 左前窗
    constexpr uint8_t kWindowFR = 1; // 右前窗
    constexpr uint8_t kWindowRL = 2; // 左后窗
    constexpr uint8_t kWindowRR = 3; // 右后窗
    constexpr uint8_t kTrunk    = 4; // 后备箱
    constexpr uint8_t kLock     = 5; // 中控锁
    constexpr uint8_t kCount    = 6;
}

// ── 空调字段索引 ──
namespace ac_item {
    constexpr uint8_t kAcSwitch      = 0; // AC 开关
    constexpr uint8_t kFanSpeed      = 1; // 风量 0-7
    constexpr uint8_t kTargetTemp    = 2; // 设定温度 16-32℃
    constexpr uint8_t kRecirculation = 3; // 内外循环 0=外 1=内
    constexpr uint8_t kCount         = 4;
}

// ── 固定大小协议结构体（SOCK_SEQPACKET，保留消息边界） ──

#pragma pack(push, 1)
struct CarMsgReq {
    uint16_t magic;     // kCarMsgMagic
    uint8_t  version;   // 协议版本 = 1
    uint8_t  cmd_type;  // CmdType
    uint8_t  mod_id;    // ModId
    uint8_t  item_id;   // 模块内字段索引
    uint8_t  value[8];  // 写入值或预留（READ/GET_ALL 忽略）
};
static_assert(sizeof(CarMsgReq) == 14, "CarMsgReq must be 14 bytes");

struct CarMsgResp {
    uint16_t magic;      // kCarMsgMagic
    uint8_t  version;    // 协议版本 = 1
    uint8_t  result;     // ResultCode
    uint8_t  _pad;       // 对齐
    uint8_t  value[16];  // 响应数据（GET_ALL 时返回模块全量状态）
};
static_assert(sizeof(CarMsgResp) == 21, "CarMsgResp must be 21 bytes");
#pragma pack(pop)

// ── 辅助函数：构造请求/响应 ──

inline CarMsgReq makeReq(ModId mod, CmdType cmd, uint8_t item, uint8_t val = 0) {
    CarMsgReq req{};
    req.magic   = kCarMsgMagic;
    req.version = 1;
    req.cmd_type = static_cast<uint8_t>(cmd);
    req.mod_id  = static_cast<uint8_t>(mod);
    req.item_id = item;
    req.value[0] = val;
    return req;
}

inline CarMsgResp makeResp(ResultCode code) {
    CarMsgResp resp{};
    resp.magic   = kCarMsgMagic;
    resp.version = 1;
    resp.result  = static_cast<uint8_t>(code);
    return resp;
}
