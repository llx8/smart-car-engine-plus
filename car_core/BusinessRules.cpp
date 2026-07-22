#include "BusinessRules.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#include "CarMsg.hpp"

namespace business {

namespace {
constexpr const char* kActuatorSock = "/tmp/car_actuator.sock";
} // namespace

void autoLock(float speed_kmh, uint8_t lock_status) {
    if (speed_kmh <= 20.0f || lock_status != 0) return;

    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kActuatorSock, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        auto req = makeReq(ModId::DOOR, CmdType::WRITE, door_item::kLock, 1);
        send(fd, &req, sizeof(req), MSG_NOSIGNAL);
        // 不等响应,发完即关:落锁是 fire-and-forget
    }
    close(fd);
}

void overspeedWarn(float speed_kmh) {
    static int warn_cnt = 0;
    if (speed_kmh > 120.0f && ++warn_cnt % 20 == 0) {
        std::cerr << "[car_core] SPEED WARNING: " << speed_kmh << " km/h" << std::endl;
    }
}

uint8_t queryDoorMask() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return 0;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kActuatorSock, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd);
        return 0;
    }
    struct timeval tv{0, 200000}; // 200ms 防止长期阻塞
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    auto req = makeReq(ModId::DOOR, CmdType::GET_ALL, 0);
    send(fd, &req, sizeof(req), MSG_NOSIGNAL);
    CarMsgResp resp{};
    recv(fd, &resp, sizeof(resp), 0);
    close(fd);
    if (resp.result != static_cast<uint8_t>(ResultCode::OK)) return 0;

    uint8_t mask = 0;
    for (int i = 0; i < 5 && i < door_item::kCount; ++i) {
        if (resp.value[i]) mask |= static_cast<uint8_t>(1u << i);
    }
    return mask;
}

bool syncDoorMaskToShm(ShmBlock* buf0, ShmBlock* buf1) {
    static uint8_t last_mask = 0;
    uint8_t mask = queryDoorMask();
    if (mask == last_mask) return false;
    if (buf0) buf0->door_mask = mask;
    if (buf1) buf1->door_mask = mask;
    last_mask = mask;
    return true;
}

} // namespace business