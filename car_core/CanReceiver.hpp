#pragma once

#include <atomic>
#include <thread>

#include "CanFrame.hpp"
#include "ShmLayout.hpp"

class CanReceiver {
public:
    CanReceiver(SPSCQueue<CanData, 256>& queue, int can_notify_fd,
                std::atomic<bool>& running);
    ~CanReceiver();

    void start();
    void join();

private:
    void threadFunc();

    SPSCQueue<CanData, 256>& queue_;
    int can_notify_fd_;
    std::atomic<bool>& running_;
    std::thread thread_;
};