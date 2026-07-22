#include "BusinessRules.hpp"
#include "CanReceiver.hpp"
#include "CoreServer.hpp"
#include "DbLogger.hpp"
#include "DtcEngine.hpp"
#include "ShmLayout.hpp"
#include "Watchdog.hpp"

#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <iostream>

namespace {
std::atomic<bool> g_running{true};

void sigHandler(int) { g_running.store(false); }
} // namespace

int main() {
    std::signal(SIGINT, sigHandler);
    std::signal(SIGTERM, sigHandler);

    // ── 共享内存 + car_dashboard 子进程(由 watch dog 自持) ──
    DashboardWatchdog dash_watch{
        ftok("/tmp/car_shm", 0xCA),
        sizeof(ShmHeader) + sizeof(ShmBlock) * 2 + 64,
        "./car_dashboard/car_dashboard"};
    if (!dash_watch.init()) return 1;

    // 写 shmid / qt_notify_fd 给 start.sh 清理用
    if (FILE* f = fopen("/tmp/car_core.shmid", "w")) {
        std::fprintf(f, "%d %d\n", dash_watch.shmid(), dash_watch.qtNotifyFd());
        std::fclose(f);
    }

    // ── CAN 接收线程(vcan0 → SPSC 队列 → 通知 main) ──
    SPSCQueue<CanData, 256> queue;
    int can_notify_fd = eventfd(0, EFD_NONBLOCK);
    CanReceiver can_rx{queue, can_notify_fd, g_running};
    can_rx.start();

    // ── DTC 诊断引擎:4 个故障监控项 ──
    DtcEngine dtc;
    dtc.registerFault(dtc_codes::P0115, DtcCategory::P, 3, 0, 2); // 水温过高 → 发动机灯慢闪
    dtc.registerFault(dtc_codes::C0035, DtcCategory::C, 2, 1, 1); // 轮速异常 → ABS灯常亮
    dtc.registerFault(dtc_codes::B0020, DtcCategory::B, 3, 2, 1); // 碰撞触发 → 气囊灯常亮
    dtc.registerFault(dtc_codes::P0560, DtcCategory::P, 2, 3, 2); // 电压低   → 电池灯慢闪

    // ── 异步 DB 写线程(独立) ──
    DbLogger db{"/tmp/car_dtc.db"};

    // ── Unix socket 服务端 ──
    CoreServer core{dtc, dash_watch.header(), dash_watch.buf(0), dash_watch.buf(1)};
    core.start("/tmp/car_core.sock");

    // actuator watchdog 单独管
    ActuatorWatchdog act_watch;

    // ── epoll:同时监听 CAN 通知 + car_core 客户端连接 ──
    int epoll_fd = epoll_create1(0);
    epoll_event ev_can{};
    ev_can.events = EPOLLIN;
    ev_can.data.fd = can_notify_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, can_notify_fd, &ev_can);
    epoll_event ev_unix{};
    ev_unix.events = EPOLLIN;
    ev_unix.data.fd = core.listenFd();
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, core.listenFd(), &ev_unix);

    // ── 主循环 ──
    int tick_count = 0;
    int cycle_secs = 0;
    uint64_t now_ms = 0;
    ShmHeader* header = dash_watch.header();

    while (g_running.load(std::memory_order_acquire)) {
        epoll_event events[2];
        int nfds = epoll_wait(epoll_fd, events, 2, 100); // 100ms 超时

        // 每 10 个 tick (~1s) 跑一次 watch dog 与状态同步
        if (++tick_count >= 10) {
            tick_count = 0;
            dash_watch.tick();
            header = dash_watch.header(); // tick 可能 refork,header 指针变了
            act_watch.tick("/tmp/car_actuator.sock",
                           "./actuator_srv/actuator_srv");
            if (++cycle_secs >= 15) {
                cycle_secs = 0;
                dtc.markDrivingCycle();
            }
            business::syncDoorMaskToShm(dash_watch.buf(0), dash_watch.buf(1));
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == can_notify_fd) {
                // 消费 eventfd 计数(否则会持续触发)
                uint64_t count;
                read(can_notify_fd, &count, sizeof(count));
                CanData data;
                while (queue.pop(data)) {
                    // Ping-Pong 双缓冲:写非活跃 buf
                    uint32_t active = header->read_index.load(std::memory_order_relaxed);
                    uint32_t target = 1 - active;
                    ShmBlock* wbuf = dash_watch.buf(target);

                    wbuf->speed_kmh       = data.speed_kmh;
                    wbuf->engine_rpm      = data.engine_rpm;
                    wbuf->water_temp_c    = data.water_temp_c;
                    wbuf->oil_temp_c     = data.oil_temp_c;
                    wbuf->fuel_percent    = data.fuel_percent;
                    wbuf->battery_voltage = data.battery_voltage;
                    wbuf->gear            = data.gear;
                    wbuf->hand_brake      = data.hand_brake;
                    wbuf->lock_status     = data.lock_status;

                    // DTC 诊断 + 故障灯
                    now_ms += 50;
                    dtc.update(*wbuf, now_ms);
                    wbuf->fault_lamp_mask = dtc.faultLampMask();
                    dtc.fillBlinkModes(wbuf->fault_blink);

                    // 异步落盘 DTC 记录
                    for (const auto& rec : dtc.activeDtcs()) {
                        DbEntry entry{};
                        entry.code = rec.code;
                        entry.severity = rec.severity;
                        entry.confirmed_ms = rec.confirmed_ms;
                        std::strncpy(entry.text, rec.toString().c_str(),
                                     sizeof(entry.text) - 1);
                        entry.text[sizeof(entry.text) - 1] = '\0';
                        entry.freeze = rec.freeze;
                        db.push(entry);
                    }

                    business::autoLock(data.speed_kmh, data.lock_status);
                    business::overspeedWarn(data.speed_kmh);

                    // 原子切换 read_index + 提示活跃性 + 通知 Qt
                    std::atomic_thread_fence(std::memory_order_release);
                    header->read_index.store(target, std::memory_order_release);
                    header->version.fetch_add(1, std::memory_order_release);

                    uint64_t one = 1;
                    write(dash_watch.qtNotifyFd(), &one, sizeof(one));
                }
            } else if (events[i].data.fd == core.listenFd()) {
                int client_fd = accept(core.listenFd(), nullptr, nullptr);
                if (client_fd >= 0) core.handleClient(client_fd);
            }
        }
    }

    // 清理
    close(can_notify_fd);
    close(epoll_fd);
    core.stop();
    db.shutdown();
    can_rx.join();
    return 0;
}