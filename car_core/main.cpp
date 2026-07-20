#include <linux/can.h> 
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/shm.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <net/if.h>
#include <sys/types.h>
#include <unistd.h>
#include <thread>
#include <atomic>
#include <cstring>
#include <iostream>
#include <csignal>
#include "CanFrame.hpp"
#include <ShmLayout.hpp>

// 全局变量
SPSCQueue<CanData, 256> g_queue; // 车况数据队列
int g_can_fd = -1; // CAN 套接字文件描述符
int g_shmid = -1; // 共享内存 ID
int g_can_notify_fd = -1; // CAN 通知套接字文件描述符
int g_qt_notify_fd = -1; // Qt 通知套接字文件描述符
ShmBlock* g_shm = nullptr; // 共享内存指针
std::atomic<bool> g_running{true}; // 运行标志

// 函数声明
void can_thread_func(); // CAN 接收线程函数
void signal_handler(int); // 信号处理函数
int main(); // 主函数

// 函数实现
// CAN 接收线程函数
void can_thread_func() {
    // 创建CAN socket
    int fd = socket(AF_CAN, SOCK_RAW, CAN_RAW);
    if (fd < 0) {
        // 打印日志位置
        std::cerr << "Error creating CAN socket" << std::endl;
        return;
    }

    // 设置100ms超时
    struct timeval tv = {0, 100000}; // 100ms
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

    // 绑定CAN接口
    struct sockaddr_can addr;
    memset(&addr, 0, sizeof(addr));
    addr.can_family = AF_CAN;
    addr.can_ifindex = if_nametoindex("vcan0");
    if (bind(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        // 打印日志位置
        std::cerr << "Error binding CAN socket" << std::endl;
        close(fd);
        return;
    }

    // 循环接收CAN帧
    while (g_running.load(std::memory_order_acquire)) {
        // 读取CAN帧
        struct can_frame frame;
        int n = read(fd, &frame, sizeof(frame));

        if (n != sizeof(frame)) {
            // 超时或错误，继续循环
            continue;
        }

        // 解析CAN帧
        CanData data;
        decode_can_frame(frame, data);

        if (g_queue.push(data)) {
            // 入队成功通知主线程
            uint64_t one = 1;
            write(g_can_notify_fd, &one, sizeof(one));
        }
    } 
    close(fd);
}

void signal_handler(int) {
    g_running.store(false);
}

int main() {
    // 注册型号处理函数
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 创建共享内存
    // 共享内存大小 = header + block + 64字节对齐余量
    constexpr size_t kShmSize = sizeof(ShmHeader) + sizeof(ShmBlock) + 64;

    key_t key = ftok("/tmp/car_shm", 0xCA);
    g_shmid = shmget(key, kShmSize, IPC_CREAT | 0666);
    if (g_shmid < 0) {
        std::cerr << "shmget failed" << std::endl;
        return 1;
    }
    void* raw = shmat(g_shmid, nullptr, 0);
    if (raw == (void*)-1) {
        std::cerr << "shmat failed" << std::endl;
        return 1;
    }

    // 手动对齐到 64 字节边界
    uintptr_t aligned = ((uintptr_t)raw + 63) & ~(uintptr_t)63;

    // 布局：[ShmHeader (32B)] [ShmBlock (~164B)]
    ShmHeader* header = reinterpret_cast<ShmHeader*>(aligned);
    g_shm = reinterpret_cast<ShmBlock*>(aligned + sizeof(ShmHeader));

    // 初始化 header
    memset(header, 0, sizeof(ShmHeader));
    header->magic = 0xCAFE0001;
    header->version.store(0, std::memory_order_relaxed);

    // 创建两个eventfd
    g_can_notify_fd = eventfd(0, EFD_NONBLOCK);
    g_qt_notify_fd = eventfd(0, EFD_NONBLOCK);

    // 启动CAN接收线程
    std::thread can_thread(can_thread_func);

    // epoll循环
    int epoll_fd = epoll_create1(0);
    struct epoll_event ev_can;
    ev_can.events = EPOLLIN;
    ev_can.data.fd = g_can_notify_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, g_can_notify_fd, &ev_can);

    while (g_running.load(std::memory_order_acquire)) {
        struct epoll_event events[1];
        int nfds = epoll_wait(epoll_fd, events, 1, 100); // 100ms超时
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == g_can_notify_fd) {
                // CAN线程通知
                uint64_t count;
                read(g_can_notify_fd, &count, sizeof(count));
                CanData data;
                while (g_queue.pop(data)) {
                    // Seqlock 写锁：偶数→奇数
                    header->version.fetch_add(1, std::memory_order_acq_rel);
                    std::atomic_thread_fence(std::memory_order_release);

                    // 更新数据
                    g_shm->speed_kmh      = data.speed_kmh;
                    g_shm->engine_rpm     = data.engine_rpm;
                    g_shm->water_temp_c   = data.water_temp_c;
                    g_shm->oil_temp_c     = data.oil_temp_c;
                    g_shm->fuel_percent   = data.fuel_percent;
                    g_shm->battery_voltage = data.battery_voltage;
                    g_shm->gear           = data.gear;
                    g_shm->hand_brake     = data.hand_brake;
                    g_shm->lock_status    = data.lock_status;

                    // Seqlock 写锁：奇数→偶数
                    std::atomic_thread_fence(std::memory_order_release);
                    header->version.fetch_add(1, std::memory_order_release);

                    // 通知 Qt
                    uint64_t one = 1;
                    write(g_qt_notify_fd, &one, sizeof(one));
                }
            }
        }
    }
    // 清理资源
    can_thread.join();
    close(g_can_notify_fd);
    close(g_qt_notify_fd);
    close(epoll_fd);
    shmdt(raw);
    shmctl(g_shmid, IPC_RMID, nullptr);
    return 0;
}