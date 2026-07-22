#include "CoreServer.hpp"

#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

CoreServer::CoreServer(DtcEngine& dtc, ShmHeader* header,
                       ShmBlock* buf0, ShmBlock* buf1)
    : dtc_(dtc), header_(header) {
    buf_[0] = buf0;
    buf_[1] = buf1;
}

CoreServer::~CoreServer() { stop(); }

bool CoreServer::start(const char* sock_path) {
    sock_path_ = sock_path;
    unlink(sock_path);
    listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (listen_fd_ < 0) return false;

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    chmod(sock_path, 0666); // 允许非 root 用户 connect
    if (listen(listen_fd_, 5) != 0) {
        close(listen_fd_);
        listen_fd_ = -1;
        return false;
    }
    std::cout << "[CoreServer] listening on " << sock_path << std::endl;
    return true;
}

void CoreServer::stop() {
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (!sock_path_.empty()) {
        unlink(sock_path_.c_str());
        sock_path_.clear();
    }
}

void CoreServer::handleClient(int client_fd) {
    CarMsgReq req{};
    ssize_t n = recv(client_fd, &req, sizeof(req), 0);
    if (n != sizeof(req)) {
        close(client_fd);
        return;
    }
    if (req.magic != kCarMsgMagic) {
        auto resp = makeResp(ResultCode::ERR);
        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        close(client_fd);
        return;
    }

    auto mod = static_cast<ModId>(req.mod_id);
    auto cmd = static_cast<CmdType>(req.cmd_type);

    // ── DTC 本地处理 ──
    if (mod == ModId::DTC) {
        auto resp = makeResp(ResultCode::OK);
        if (cmd == CmdType::GET_ALL) {
            const auto& list = dtc_.activeDtcs();
            resp.value[0] = static_cast<uint8_t>(list.size());
            for (size_t i = 0; i < list.size() && i < 3; ++i) {
                uint32_t code = list[i].code;
                resp.value[1 + i * 5 + 0] = static_cast<uint8_t>((code >> 24) & 0xFF);
                resp.value[1 + i * 5 + 1] = static_cast<uint8_t>((code >> 16) & 0xFF);
                resp.value[1 + i * 5 + 2] = static_cast<uint8_t>((code >> 8) & 0xFF);
                resp.value[1 + i * 5 + 3] = static_cast<uint8_t>(code & 0xFF);
                resp.value[1 + i * 5 + 4] = list[i].severity;
            }
        } else if (cmd == CmdType::WRITE && req.value[0] == 1) {
            dtc_.clearAll();
        } else if (cmd == CmdType::READ) {
            const auto& list = dtc_.activeDtcs();
            size_t idx = req.value[0];
            if (idx < list.size()) {
                const auto& ff = list[idx].freeze;
                std::memcpy(resp.value,      &ff.speed_kmh,      4);
                std::memcpy(resp.value + 4,  &ff.engine_rpm,     4);
                std::memcpy(resp.value + 8,  &ff.water_temp_c,   4);
                std::memcpy(resp.value + 12, &ff.battery_voltage, 4);
            }
        }
        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        close(client_fd);
        return;
    }

    // ── AI 状态通知 ──
    if (mod == ModId::AI) {
        if (cmd == CmdType::WRITE && req.item_id == ai_item::kStatus) {
            int32_t st = req.value[0] ? 1 : 0;
            for (auto* b : buf_) {
                if (!b) continue;
                b->ai_status = st;
                if (st)
                    std::strncpy(b->ai_message, "AI service unavailable",
                                 sizeof(b->ai_message) - 1);
                else
                    b->ai_message[0] = '\0';
            }
        }
        auto resp = makeResp(ResultCode::OK);
        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        close(client_fd);
        return;
    }

    // ── 安全拦截:行驶中(speed>5km/h)禁止操作车窗/车门/后备箱(中控锁除外) ──
    if (mod == ModId::DOOR && cmd == CmdType::WRITE
        && req.item_id < door_item::kLock) {
        if (header_ && buf_[0]) {
            uint32_t active = header_->read_index.load(std::memory_order_acquire);
            if (buf_[active]->speed_kmh > 5.0f) {
                auto resp = makeResp(ResultCode::SAFETY_BLOCK);
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                close(client_fd);
                return;
            }
        }
    }

    // ── 代理到 actuator_srv ──
    int act_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (act_fd < 0) {
        close(client_fd);
        return;
    }
    struct sockaddr_un act_addr{};
    act_addr.sun_family = AF_UNIX;
    std::strncpy(act_addr.sun_path, kActuatorSock, sizeof(act_addr.sun_path) - 1);
    if (connect(act_fd, reinterpret_cast<struct sockaddr*>(&act_addr), sizeof(act_addr)) < 0) {
        auto resp = makeResp(ResultCode::ERR);
        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        close(act_fd);
        close(client_fd);
        return;
    }
    send(act_fd, &req, sizeof(req), MSG_NOSIGNAL);
    CarMsgResp resp{};
    recv(act_fd, &resp, sizeof(resp), 0);
    close(act_fd);
    send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
    close(client_fd);
}