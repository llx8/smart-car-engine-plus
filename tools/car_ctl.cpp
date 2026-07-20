#include "CarMsg.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <iostream>

// ── CLI 调试工具：连接 car_core（car_core 代理到 actuator_srv） ──
// 用法:
//   car_ctl door read <item>        — 读门控字段
//   car_ctl door write <item> <val> — 写门控字段
//   car_ctl door get_all            — 获取门控全量状态
//   car_ctl ac read <item>          — 读空调字段
//   car_ctl ac write <item> <val>   — 写空调字段
//   car_ctl ac get_all              — 获取空调全量状态
//   car_ctl dtc list                — 列出当前活跃 DTC
//   car_ctl dtc clear               — 清除所有 DTC

static int connectToServer() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        std::cerr << "socket() failed" << std::endl;
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/car_core.sock", sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "connect to /tmp/car_core.sock failed (is car_core running?)" << std::endl;
        close(fd);
        return -1;
    }
    return fd;
}

static void printDoorAll(const CarMsgResp& resp) {
    const char* names[] = {"左前窗", "右前窗", "左后窗", "右后窗", "后备箱", "中控锁"};
    std::cout << "门控状态:" << std::endl;
    for (int i = 0; i < door_item::kCount; ++i) {
        std::cout << "  " << names[i] << ": " << (resp.value[i] ? "开" : "关") << std::endl;
    }
}

static void printAcAll(const CarMsgResp& resp) {
    std::cout << "空调状态:" << std::endl;
    std::cout << "  AC 开关: " << (resp.value[ac_item::kAcSwitch] ? "开" : "关") << std::endl;
    std::cout << "  风量:    " << (int)resp.value[ac_item::kFanSpeed] << " / 7" << std::endl;
    std::cout << "  设定温度: " << (int)resp.value[ac_item::kTargetTemp] << " °C" << std::endl;
    std::cout << "  循环模式: " << (resp.value[ac_item::kRecirculation] ? "内循环" : "外循环") << std::endl;
}

static const char* resultStr(uint8_t code) {
    switch (static_cast<ResultCode>(code)) {
    case ResultCode::OK:           return "OK";
    case ResultCode::ERR:          return "ERR";
    case ResultCode::UNKNOWN_CMD:  return "UNKNOWN_CMD";
    case ResultCode::UNKNOWN_MOD:  return "UNKNOWN_MOD";
    case ResultCode::UNKNOWN_ITEM: return "UNKNOWN_ITEM";
    case ResultCode::OUT_OF_RANGE: return "OUT_OF_RANGE";
    case ResultCode::SAFETY_BLOCK: return "SAFETY_BLOCK";
    default:                       return "???";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 3) {
        std::cerr << "用法: car_ctl <door|ac> <read|write|get_all> [item] [value]\n"
                     "      car_ctl dtc <list|clear>"
                  << std::endl;
        return 1;
    }

    // 解析参数
    const char* module = argv[1];
    const char* cmd    = argv[2];

    // ── DTC 子命令 ──
    if (std::strcmp(module, "dtc") == 0) {
        if (std::strcmp(cmd, "list") == 0) {
            int fd = connectToServer();
            if (fd < 0) return 1;
            CarMsgReq req = makeReq(ModId::DTC, CmdType::GET_ALL, dtc_item::kList);
            send(fd, &req, sizeof(req), MSG_NOSIGNAL);
            CarMsgResp resp{};
            recv(fd, &resp, sizeof(resp), 0);
            close(fd);
            if (resp.magic != kCarMsgMagic) {
                std::cerr << "响应 magic 不匹配" << std::endl;
                return 1;
            }
            uint8_t count = resp.value[0];
            if (count == 0) {
                std::cout << "(无活跃 DTC)" << std::endl;
                return 0;
            }
            std::cout << "活跃 DTC (" << (int)count << "):" << std::endl;
            for (size_t i = 0; i < count && i < 3; ++i) {
                uint32_t code = (uint32_t(resp.value[1 + i*5 + 0]) << 24)
                              | (uint32_t(resp.value[1 + i*5 + 1]) << 16)
                              | (uint32_t(resp.value[1 + i*5 + 2]) << 8)
                              |  uint32_t(resp.value[1 + i*5 + 3]);
                uint8_t sev   = resp.value[1 + i*5 + 4];
                // 编码方案: code>>24 取 ASCII 字符, & 0xFFFF 取编号
                char cat = static_cast<char>((code >> 24) & 0xFF);
                char buf[8]{};
                std::snprintf(buf, sizeof(buf), "%c%04X", cat, code & 0xFFFF);
                std::cout << "  [" << (i+1) << "] " << buf
                          << "  severity=" << (int)sev << std::endl;
            }
            return 0;
        }
        if (std::strcmp(cmd, "clear") == 0) {
        int fd = connectToServer();
        if (fd < 0) return 1;
        CarMsgReq req = makeReq(ModId::DTC, CmdType::WRITE, dtc_item::kClear, 1);
        send(fd, &req, sizeof(req), MSG_NOSIGNAL);
        CarMsgResp resp{};
        recv(fd, &resp, sizeof(resp), 0);
        close(fd);
        if (resp.result != static_cast<uint8_t>(ResultCode::OK)) {
            std::cerr << "清除失败: " << resultStr(resp.result) << std::endl;
            return 1;
        }
        std::cout << "DTC 已清除" << std::endl;
        return 0;
    }
    if (std::strcmp(cmd, "freeze") == 0) {
        if (argc < 4) {
            std::cerr << "用法: car_ctl dtc freeze <idx>" << std::endl;
            return 1;
        }
        uint8_t idx = static_cast<uint8_t>(std::atoi(argv[3]));
        int fd = connectToServer();
        if (fd < 0) return 1;
        // car_core 用 req.value[0] 作为 index
        CarMsgReq req = makeReq(ModId::DTC, CmdType::READ, 1, idx);
        send(fd, &req, sizeof(req), MSG_NOSIGNAL);
        CarMsgResp resp{};
        recv(fd, &resp, sizeof(resp), 0);
        close(fd);
        if (resp.result != static_cast<uint8_t>(ResultCode::OK)) {
            std::cerr << "查询失败: " << resultStr(resp.result) << std::endl;
            return 1;
        }
        float speed, water, battery;
        int32_t rpm;
        std::memcpy(&speed,   resp.value,      4);
        std::memcpy(&rpm,     resp.value + 4,  4);
        std::memcpy(&water,   resp.value + 8,  4);
        std::memcpy(&battery, resp.value + 12, 4);
        std::cout << "冻结帧 [" << (int)idx << "]:" << std::endl;
        std::cout << "  车速: " << speed   << " km/h" << std::endl;
        std::cout << "  转速: " << rpm     << " rpm" << std::endl;
        std::cout << "  水温: " << water   << " C"   << std::endl;
        std::cout << "  电瓶: " << battery << " V"   << std::endl;
        return 0;
    }
    std::cerr << "未知 dtc 命令: " << cmd << " (应为 list/clear/freeze)" << std::endl;
    return 1;
}

    ModId modId;
    if (std::strcmp(module, "door") == 0)      modId = ModId::DOOR;
    else if (std::strcmp(module, "ac") == 0)   modId = ModId::AC;
    else {
        std::cerr << "未知模块: " << module << " (应为 door/ac/dtc)" << std::endl;
        return 1;
    }

    CmdType cmdType;
    if (std::strcmp(cmd, "read") == 0)         cmdType = CmdType::READ;
    else if (std::strcmp(cmd, "write") == 0)   cmdType = CmdType::WRITE;
    else if (std::strcmp(cmd, "get_all") == 0) cmdType = CmdType::GET_ALL;
    else {
        std::cerr << "未知命令: " << cmd << " (应为 read/write/get_all)" << std::endl;
        return 1;
    }

    uint8_t item = 0;
    uint8_t val  = 0;

    if (cmdType == CmdType::READ || cmdType == CmdType::WRITE) {
        if (argc < 4) {
            std::cerr << "缺少 item 参数" << std::endl;
            return 1;
        }
        item = static_cast<uint8_t>(std::atoi(argv[3]));
    }
    if (cmdType == CmdType::WRITE) {
        if (argc < 5) {
            std::cerr << "缺少 value 参数" << std::endl;
            return 1;
        }
        val = static_cast<uint8_t>(std::atoi(argv[4]));
    }

    // 连接执行模块
    int fd = connectToServer();
    if (fd < 0) return 1;

    // 发送请求
    CarMsgReq req = makeReq(modId, cmdType, item, val);
    send(fd, &req, sizeof(req), MSG_NOSIGNAL);

    // 接收响应
    CarMsgResp resp{};
    recv(fd, &resp, sizeof(resp), 0);
    close(fd);

    // 校验响应 magic
    if (resp.magic != kCarMsgMagic) {
        std::cerr << "响应 magic 不匹配" << std::endl;
        return 1;
    }

    // 打印结果
    if (resp.result != static_cast<uint8_t>(ResultCode::OK)) {
        std::cerr << "错误: " << resultStr(resp.result) << std::endl;
        return 1;
    }

    if (cmdType == CmdType::READ) {
        std::cout << "值: " << (int)resp.value[0] << std::endl;
    } else if (cmdType == CmdType::WRITE) {
        std::cout << "写入成功" << std::endl;
    } else if (cmdType == CmdType::GET_ALL) {
        if (modId == ModId::DOOR) printDoorAll(resp);
        else                      printAcAll(resp);
    }

    return 0;
}
