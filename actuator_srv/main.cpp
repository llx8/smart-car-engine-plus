#include "ModuleServer.hpp"
#include "CarMsg.hpp"
#include <cstring>
#include <iostream>

// ── 执行模块：门控（6 状态位 + 中控锁）+ 空调（4 字段） ──
// 通过 mod_id 路由，单一进程通过 onRequest 虚函数处理所有模块

class ActuatorServer : public ModuleServer {
public:
    ActuatorServer() : ModuleServer("/tmp/car_actuator.sock") {}

protected:
    CarMsgResp onRequest(const CarMsgReq& req) override {
        ModId mod = static_cast<ModId>(req.mod_id);
        CmdType cmd = static_cast<CmdType>(req.cmd_type);

        switch (mod) {
        case ModId::DOOR: return handleDoor(cmd, req.item_id, req.value[0]);
        case ModId::AC:   return handleAc(cmd, req.item_id, req.value[0]);
        default:          return makeResp(ResultCode::UNKNOWN_MOD);
        }
    }

private:
    // ── 门控状态 ──
    uint8_t door_state_[door_item::kCount] = {0}; // 0=关 1=开

    // ── 空调状态 ──
    uint8_t ac_switch_       = 0;  // 0=关 1=开
    uint8_t fan_speed_       = 3;  // 0-7，默认 3
    uint8_t target_temp_     = 24; // 16-32℃，默认 24
    uint8_t recirculation_   = 0;  // 0=外循环 1=内循环

    CarMsgResp handleDoor(CmdType cmd, uint8_t item, uint8_t val) {
        if (item >= door_item::kCount) return makeResp(ResultCode::UNKNOWN_ITEM);

        switch (cmd) {
        case CmdType::READ: {
            auto resp = makeResp(ResultCode::OK);
            resp.value[0] = door_state_[item];
            return resp;
        }
        case CmdType::WRITE: {
            if (val > 1) return makeResp(ResultCode::OUT_OF_RANGE);
            door_state_[item] = val;
            std::cout << "[Door] item=" << (int)item
                      << " set to " << (int)val << std::endl;
            return makeResp(ResultCode::OK);
        }
        case CmdType::GET_ALL: {
            auto resp = makeResp(ResultCode::OK);
            std::memcpy(resp.value, door_state_, door_item::kCount);
            return resp;
        }
        default: return makeResp(ResultCode::UNKNOWN_CMD);
        }
    }

    CarMsgResp handleAc(CmdType cmd, uint8_t item, uint8_t val) {
        if (item >= ac_item::kCount) return makeResp(ResultCode::UNKNOWN_ITEM);

        switch (cmd) {
        case CmdType::READ: {
            auto resp = makeResp(ResultCode::OK);
            switch (item) {
            case ac_item::kAcSwitch:      resp.value[0] = ac_switch_;     break;
            case ac_item::kFanSpeed:      resp.value[0] = fan_speed_;     break;
            case ac_item::kTargetTemp:    resp.value[0] = target_temp_;   break;
            case ac_item::kRecirculation: resp.value[0] = recirculation_; break;
            }
            return resp;
        }
        case CmdType::WRITE: {
            switch (item) {
            case ac_item::kAcSwitch:
                if (val > 1) return makeResp(ResultCode::OUT_OF_RANGE);
                ac_switch_ = val; break;
            case ac_item::kFanSpeed:
                if (val > 7) return makeResp(ResultCode::OUT_OF_RANGE);
                fan_speed_ = val; break;
            case ac_item::kTargetTemp:
                if (val < 16 || val > 32) return makeResp(ResultCode::OUT_OF_RANGE);
                target_temp_ = val; break;
            case ac_item::kRecirculation:
                if (val > 1) return makeResp(ResultCode::OUT_OF_RANGE);
                recirculation_ = val; break;
            }
            std::cout << "[AC] item=" << (int)item
                      << " set to " << (int)val << std::endl;
            return makeResp(ResultCode::OK);
        }
        case CmdType::GET_ALL: {
            auto resp = makeResp(ResultCode::OK);
            resp.value[0] = ac_switch_;
            resp.value[1] = fan_speed_;
            resp.value[2] = target_temp_;
            resp.value[3] = recirculation_;
            return resp;
        }
        default: return makeResp(ResultCode::UNKNOWN_CMD);
        }
    }
};

int main()
{
    ActuatorServer server;
    server.run();
    return 0;
}
