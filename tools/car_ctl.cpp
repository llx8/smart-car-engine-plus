#include "CarMsg.hpp"
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <iostream>

// ── CLI 调试工具：直接连接 actuator_srv ──
// 用法:
//   car_ctl actuator door read <item>        — 读门控字段
//   car_ctl actuator door write <item> <val> — 写门控字段
//   car_ctl actuator door get_all            — 获取门控全量状态
//   car_ctl actuator ac read <item>          — 读空调字段
//   car_ctl actuator ac write <item> <val>   — 写空调字段
//   car_ctl actuator ac get_all              — 获取空调全量状态

static int connectToServer() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        std::cerr << "socket() failed" << std::endl;
        return -1;
    }

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, "/tmp/car_actuator.sock", sizeof(addr.sun_path) - 1);

    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "connect to /tmp/car_actuator.sock failed" << std::endl;
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
    default:                       return "???";
    }
}

int main(int argc, char* argv[]) {
    if (argc < 4) {
        std::cerr << "用法: car_ctl actuator <door|ac> <read|write|get_all> [item] [value]"
                  << std::endl;
        return 1;
    }

    // 解析参数
    const char* module = argv[2];
    const char* cmd    = argv[3];

    ModId modId;
    if (std::strcmp(module, "door") == 0)      modId = ModId::DOOR;
    else if (std::strcmp(module, "ac") == 0)   modId = ModId::AC;
    else {
        std::cerr << "未知模块: " << module << " (应为 door 或 ac)" << std::endl;
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
        if (argc < 5) {
            std::cerr << "缺少 item 参数" << std::endl;
            return 1;
        }
        item = static_cast<uint8_t>(std::atoi(argv[4]));
    }
    if (cmdType == CmdType::WRITE) {
        if (argc < 6) {
            std::cerr << "缺少 value 参数" << std::endl;
            return 1;
        }
        val = static_cast<uint8_t>(std::atoi(argv[5]));
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
