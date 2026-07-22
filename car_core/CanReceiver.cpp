#include "CanReceiver.hpp"

#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

CanReceiver::CanReceiver(SPSCQueue<CanData, 256>& queue, int can_notify_fd,
                         std::atomic<bool>& running)
    : queue_(queue), can_notify_fd_(can_notify_fd), running_(running) {}

CanReceiver::~CanReceiver() { join(); }

void CanReceiver::start() {
    thread_ = std::thread(&CanReceiver::threadFunc, this);
}

void CanReceiver::join() {
    if (thread_.joinable()) thread_.join();
}

void CanReceiver::threadFunc() {
    int fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        std::cerr << "[CanReceiver] socket() failed" << std::endl;
        return;
    }

    struct timeval tv{0, 100000}; // 100ms 超时,让线程能周期性检查 running
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    struct sockaddr_can addr{};
    addr.can_family = AF_CAN;
    addr.can_ifindex = if_nametoindex("vcan0");
    if (bind(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[CanReceiver] bind(vcan0) failed" << std::endl;
        close(fd);
        return;
    }

    while (running_.load(std::memory_order_acquire)) {
        struct can_frame frame;
        int n = read(fd, &frame, sizeof(frame));
        if (n != sizeof(frame)) continue;

        CanData data;
        decode_can_frame(frame, data);
        if (queue_.push(data)) {
            uint64_t one = 1;
            write(can_notify_fd_, &one, sizeof(one));
        }
    }
    close(fd);
}