#pragma once

#include <string>

#include "CarMsg.hpp"
#include "DtcEngine.hpp"
#include "ShmLayout.hpp"

// car_core.sock 服务端:统一处理 Qt/CLI/AI 客户端请求
// 三类路由:
//   - DTC 查询/清除/读冻结帧 → 本地 DtcEngine
//   - AI 状态通知 → 直接写到两个 ShmBlock
//   - DOOR/AC → 行驶安全拦截后代理转发到 actuator_srv
class CoreServer {
public:
    CoreServer(DtcEngine& dtc, ShmHeader* header, ShmBlock* buf0, ShmBlock* buf1);
    ~CoreServer();

    bool start(const char* sock_path);
    int listenFd() const { return listen_fd_; }
    void handleClient(int client_fd);
    void stop();

private:
    DtcEngine& dtc_;
    ShmHeader* header_;
    ShmBlock*  buf_[2];
    int listen_fd_ = -1;
    std::string sock_path_;

    static constexpr const char* kActuatorSock = "/tmp/car_actuator.sock";
};