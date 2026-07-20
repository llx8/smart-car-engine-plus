#include <linux/can.h> 
#include <linux/can/raw.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/shm.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <net/if.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/wait.h>
#include <thread>
#include <atomic>
#include <cstring>
#include <iostream>
#include <csignal>
#include "CanFrame.hpp"
#include <ShmLayout.hpp>
#include <CarMsg.hpp>
#include <DtcEngine.hpp>
#include <sqlite3.h>

// 全局变量
SPSCQueue<CanData, 256> g_queue; // 车况数据队列
int g_can_fd = -1; // CAN 套接字文件描述符
int g_shmid = -1; // 共享内存 ID
int g_can_notify_fd = -1; // CAN 通知套接字文件描述符
int g_qt_notify_fd = -1; // Qt 通知套接字文件描述符
ShmBlock* g_shm_buf[2] = {nullptr, nullptr};  // Ping-Pong 双缓冲
ShmHeader* g_header = nullptr;  // 共享内存头（供 handle_car_core_client 安全检查用）
std::atomic<bool> g_running{true}; // 运行标志
int g_listen_fd = -1; // Unix socket 监听 fd
static const char* kSockPath = "/tmp/car_core.sock";
DtcEngine* g_dtc = nullptr; // DTC 引擎指针（供 handle_car_core_client 访问）
static const char* kDbPath = "/tmp/car_dtc.db";

// ── DB 线程: SPSC 无锁队列 + 独立线程写 SQLite ──
// 主线程 push DbEntry,DB 线程 pop + prepare/bind/step/finalize
struct DbEntry {
    uint32_t   code;
    uint8_t    severity;
    char       text[16];
    uint64_t   confirmed_ms;
    FreezeFrame freeze;
};
static SPSCQueue<DbEntry, 64> g_db_queue;
static std::thread g_db_thread;
static std::atomic<bool> g_db_running{true};

static void db_thread_func() {
    sqlite3* db = nullptr;
    if (sqlite3_open(kDbPath, &db) != SQLITE_OK) return;
    const char* ddl = "CREATE TABLE IF NOT EXISTS dtc_log ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  dtc_code INTEGER, dtc_text TEXT, severity INTEGER,"
        "  first_seen INTEGER, confirmed_at INTEGER, active INTEGER,"
        "  speed REAL, rpm INTEGER, water_temp REAL, oil_temp REAL,"
        "  fuel REAL, battery REAL);";
    sqlite3_exec(db, ddl, nullptr, nullptr, nullptr);

    const char* ins = "INSERT INTO dtc_log "
        "(dtc_code, dtc_text, severity, first_seen, confirmed_at, active,"
        " speed, rpm, water_temp, oil_temp, fuel, battery)"
        " VALUES (?,?,?,?,?,1, ?,?,?,?,?,?);";

    while (g_db_running.load(std::memory_order_acquire)) {
        DbEntry entry;
        if (!g_db_queue.pop(entry)) {
            usleep(5000);
            continue;
        }
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_int(stmt, 1, static_cast<int>(entry.code));
        sqlite3_bind_text(stmt, 2, entry.text, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, entry.severity);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(entry.freeze.timestamp_ms));
        sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(entry.confirmed_ms));
        sqlite3_bind_double(stmt, 6, entry.freeze.speed_kmh);
        sqlite3_bind_int(stmt, 7, entry.freeze.engine_rpm);
        sqlite3_bind_double(stmt, 8, entry.freeze.water_temp_c);
        sqlite3_bind_double(stmt, 9, entry.freeze.oil_temp_c);
        sqlite3_bind_double(stmt, 10, entry.freeze.fuel_percent);
        sqlite3_bind_double(stmt, 11, entry.freeze.battery_voltage);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    // 排空队列中剩余数据
    DbEntry entry;
    while (g_db_queue.pop(entry)) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) continue;
        sqlite3_bind_int(stmt, 1, static_cast<int>(entry.code));
        sqlite3_bind_text(stmt, 2, entry.text, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, entry.severity);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(entry.freeze.timestamp_ms));
        sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(entry.confirmed_ms));
        sqlite3_bind_double(stmt, 6, entry.freeze.speed_kmh);
        sqlite3_bind_int(stmt, 7, entry.freeze.engine_rpm);
        sqlite3_bind_double(stmt, 8, entry.freeze.water_temp_c);
        sqlite3_bind_double(stmt, 9, entry.freeze.oil_temp_c);
        sqlite3_bind_double(stmt, 10, entry.freeze.fuel_percent);
        sqlite3_bind_double(stmt, 11, entry.freeze.battery_voltage);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
    sqlite3_close(db);
}

// 同步 actuator_srv 的门控全量状态到 ShmBlock::door_mask
// 让 dashboard 能看到 AI/CLI 真实开门动作
static uint8_t queryDoorMask() {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return 0;
    struct sockaddr_un a{};
    a.sun_family = AF_UNIX;
    std::strncpy(a.sun_path, "/tmp/car_actuator.sock", sizeof(a.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&a), sizeof(a)) < 0) {
        close(fd); return 0;
    }
    auto req = makeReq(ModId::DOOR, CmdType::GET_ALL, 0);
    send(fd, &req, sizeof(req), MSG_NOSIGNAL);
    CarMsgResp resp{};
    recv(fd, &resp, sizeof(resp), 0);
    close(fd);
    if (resp.result != static_cast<uint8_t>(ResultCode::OK)) return 0;
    // ShmBlock::door_mask bit0..4 分别对应: 左前/右前/左后/右后/后备箱
    // actuator_srv door GET_ALL 返回 value[0..5] 对应 door_item::kWindowFL/FR/RL/RR/Trunk/Lock
    uint8_t mask = 0;
    for (int i = 0; i < 5 && i < door_item::kCount; ++i) {
        if (resp.value[i]) mask |= (1u << i);
    }
    return mask;
}

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

// ── Unix socket 客户端处理：代理转发到 actuator_srv ──
static void handle_car_core_client(int client_fd) {
    CarMsgReq req{};
    ssize_t n = recv(client_fd, &req, sizeof(req), 0);
    if (n != sizeof(req)) { close(client_fd); return; }
    if (req.magic != kCarMsgMagic) {
        auto resp = makeResp(ResultCode::ERR);
        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        close(client_fd);
        return;
    }

    // ── DTC 查询在 car_core 本地处理 ──
    if (static_cast<ModId>(req.mod_id) == ModId::DTC) {
        auto resp = makeResp(ResultCode::OK);
        if (g_dtc && static_cast<CmdType>(req.cmd_type) == CmdType::GET_ALL) {
            const auto& list = g_dtc->activeDtcs();
            resp.value[0] = static_cast<uint8_t>(list.size());
            for (size_t i = 0; i < list.size() && i < 3; ++i) {
                uint32_t code = list[i].code;
                resp.value[1 + i * 5 + 0] = static_cast<uint8_t>((code >> 24) & 0xFF);
                resp.value[1 + i * 5 + 1] = static_cast<uint8_t>((code >> 16) & 0xFF);
                resp.value[1 + i * 5 + 2] = static_cast<uint8_t>((code >> 8) & 0xFF);
                resp.value[1 + i * 5 + 3] = static_cast<uint8_t>(code & 0xFF);
                resp.value[1 + i * 5 + 4] = list[i].severity;
            }
        } else if (g_dtc && static_cast<CmdType>(req.cmd_type) == CmdType::WRITE
                   && req.value[0] == 1) {
            g_dtc->clearAll();
            resp = makeResp(ResultCode::OK);
        } else if (g_dtc && static_cast<CmdType>(req.cmd_type) == CmdType::READ) {
            // 读取指定索引的冻结帧 (item_id=1, index=value[1])
            const auto& list = g_dtc->activeDtcs();
            size_t idx = req.value[0];
            if (idx < list.size()) {
                const auto& ff = list[idx].freeze;
                std::memcpy(resp.value,     &ff.speed_kmh,      4);
                std::memcpy(resp.value + 4, &ff.engine_rpm,     4);
                std::memcpy(resp.value + 8, &ff.water_temp_c,   4);
                std::memcpy(resp.value + 12,&ff.battery_voltage,4);
            }
        }
        send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
        close(client_fd);
        return;
    }

    // ── 其他模块代理到 actuator_srv（含安全检查） ──

    // 安全检查：行驶中 (speed > 5 km/h) 禁止操作车窗
    if (static_cast<ModId>(req.mod_id) == ModId::DOOR
        && static_cast<CmdType>(req.cmd_type) == CmdType::WRITE
        && req.item_id <= door_item::kWindowRR) {
        // 从活跃缓冲区读取当前车速
        if (g_header && g_shm_buf[0]) {
            uint32_t active = g_header->read_index.load(
                std::memory_order_acquire);
            if (g_shm_buf[active]->speed_kmh > 5.0f) {
                auto resp = makeResp(ResultCode::SAFETY_BLOCK);
                send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
                close(client_fd);
                return;
            }
        }
    }

    int act_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (act_fd < 0) { close(client_fd); return; }

    struct sockaddr_un act_addr{};
    act_addr.sun_family = AF_UNIX;
    std::strncpy(act_addr.sun_path, "/tmp/car_actuator.sock", sizeof(act_addr.sun_path) - 1);
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

int main() {
    // 注册型号处理函数
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // 创建共享内存 — Ping-Pong 双缓冲
    // 布局: [ShmHeader (32B)] [ShmBlock buf[0]] [ShmBlock buf[1]]
    constexpr size_t kShmSize = sizeof(ShmHeader) + sizeof(ShmBlock) * 2 + 64;

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

    ShmHeader* header = reinterpret_cast<ShmHeader*>(aligned);
    g_header = header;  // 设置全局指针
    g_shm_buf[0] = reinterpret_cast<ShmBlock*>(aligned + sizeof(ShmHeader));
    g_shm_buf[1] = reinterpret_cast<ShmBlock*>(
        aligned + sizeof(ShmHeader) + sizeof(ShmBlock));

    // 初始化 header
    memset(header, 0, sizeof(ShmHeader));
    memset(g_shm_buf[0], 0, sizeof(ShmBlock));
    memset(g_shm_buf[1], 0, sizeof(ShmBlock));
    header->magic = 0xCAFE0001;
    header->version.store(0, std::memory_order_relaxed);
    header->read_index.store(0, std::memory_order_relaxed);

    // 创建两个eventfd
    g_can_notify_fd = eventfd(0, EFD_NONBLOCK);
    g_qt_notify_fd = eventfd(0, EFD_NONBLOCK);

    // ── 创建 Unix socket 服务端 ──
    unlink(kSockPath);
    g_listen_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (g_listen_fd >= 0) {
        struct sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, kSockPath, sizeof(addr.sun_path) - 1);
        if (bind(g_listen_fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
            listen(g_listen_fd, 5);
            std::cout << "[car_core] listening on " << kSockPath << std::endl;
        } else {
            close(g_listen_fd);
            g_listen_fd = -1;
        }
    }

    // 启动CAN接收线程
    std::thread can_thread(can_thread_func);

    // ── DTC 诊断引擎初始化 ──
    // 4 个故障灯,每个对应一个物理量 + 直观触发条件
    // blink_mode: 0=灭 1=常亮 2=慢闪 3=快闪 → P 类慢闪, B/C 常亮, 警告类慢闪
    DtcEngine dtc;
    dtc.registerFault(dtc_codes::P0115, DtcCategory::P, 3, 0, 2); // 水温过高 → bit0 发动机灯(慢闪)
    dtc.registerFault(dtc_codes::C0035, DtcCategory::C, 2, 1, 1); // 轮速异常 → bit1 ABS灯(常亮)
    dtc.registerFault(dtc_codes::B0020, DtcCategory::B, 3, 2, 1); // 碰撞触发 → bit2 气囊灯(常亮)
    dtc.registerFault(dtc_codes::P0560, DtcCategory::P, 2, 3, 2); // 电压低   → bit3 电池灯(慢闪)
    g_dtc = &dtc;

    // ── SQLite: 启动独立 DB 线程 ──
    g_db_thread = std::thread(db_thread_func);

    // ── 看门狗：fork car_dashboard 子进程 ──
    pid_t child_pid = -1;
    {
        pid_t pid = fork();
        if (pid == 0) {
            // 子进程 → exec car_dashboard
            char shmid_str[32], evfd_str[32];
            snprintf(shmid_str, sizeof(shmid_str), "%d", g_shmid);
            snprintf(evfd_str, sizeof(evfd_str), "%d", g_qt_notify_fd);
            execlp("./car_dashboard/car_dashboard",
                   "car_dashboard",
                   shmid_str, evfd_str,
                   nullptr);
            // execlp 失败 → 退出子进程
            std::cerr << "[car_core] failed to exec car_dashboard" << std::endl;
            _exit(1);
        } else if (pid > 0) {
            child_pid = pid;
            std::cout << "[car_core] watchdog: car_dashboard pid="
                      << child_pid << std::endl;
        }
        // pid < 0: fork 失败，car_core 继续运行（不含仪表盘）
    }

    // ── 看门狗：actuator_srv ping 状态 + 自动 fork 重启 ──
    int act_ping_fail_count = 0;
    pid_t act_child_pid = -1;  // car_core 看门狗重启的 actuator_srv 进程 PID (-1=非本进程管理)

    // 用于 DTC 时间戳（毫秒计数器）
    uint64_t now_ms = 0;

    // epoll循环
    int epoll_fd = epoll_create1(0);
    struct epoll_event ev_can;
    ev_can.events = EPOLLIN;
    ev_can.data.fd = g_can_notify_fd;
    epoll_ctl(epoll_fd, EPOLL_CTL_ADD, g_can_notify_fd, &ev_can);

    if (g_listen_fd >= 0) {
        struct epoll_event ev_unix;
        ev_unix.events = EPOLLIN;
        ev_unix.data.fd = g_listen_fd;
        epoll_ctl(epoll_fd, EPOLL_CTL_ADD, g_listen_fd, &ev_unix);
    }

    while (g_running.load(std::memory_order_acquire)) {
        struct epoll_event events[2];
        int nfds = epoll_wait(epoll_fd, events, 2, 100); // 100ms超时

        // ── 看门狗定时检查（每 10 个 tick ≈ 1 秒） ──
        static int tick_count = 0;
        if (++tick_count >= 10) {
            tick_count = 0;

            // 1) 检查 car_dashboard 子进程是否存活
            if (child_pid > 0) {
                int status = 0;
                pid_t result = waitpid(child_pid, &status, WNOHANG);
                if (result == child_pid) {
                    std::cerr << "[car_core] watchdog: car_dashboard (pid="
                              << child_pid << ") exited, restarting..."
                              << std::endl;

                    // 重建共享内存 + eventfd
                    shmdt(raw);
                    shmctl(g_shmid, IPC_RMID, nullptr);
                    close(g_qt_notify_fd);

                    g_shmid = shmget(key, kShmSize, IPC_CREAT | 0666);
                    raw = shmat(g_shmid, nullptr, 0);
                    uintptr_t aligned2 = ((uintptr_t)raw + 63) & ~(uintptr_t)63;
                    header = reinterpret_cast<ShmHeader*>(aligned2);
                    g_header = header;
                    g_shm_buf[0] = reinterpret_cast<ShmBlock*>(
                        aligned2 + sizeof(ShmHeader));
                    g_shm_buf[1] = reinterpret_cast<ShmBlock*>(
                        aligned2 + sizeof(ShmHeader) + sizeof(ShmBlock));
                    memset(header, 0, sizeof(ShmHeader));
                    memset(g_shm_buf[0], 0, sizeof(ShmBlock));
                    memset(g_shm_buf[1], 0, sizeof(ShmBlock));
                    header->magic = 0xCAFE0001;
                    header->read_index.store(0, std::memory_order_relaxed);
                    g_qt_notify_fd = eventfd(0, EFD_NONBLOCK);

                    // 重新 fork
                    pid_t new_pid = fork();
                    if (new_pid == 0) {
                        char s[32], e[32];
                        snprintf(s, sizeof(s), "%d", g_shmid);
                        snprintf(e, sizeof(e), "%d", g_qt_notify_fd);
                        execlp("./car_dashboard/car_dashboard",
                               "car_dashboard",
                               s, e, nullptr);
                        _exit(1);
                    } else if (new_pid > 0) {
                        child_pid = new_pid;
                        std::cout << "[car_core] watchdog: restarted car_dashboard pid="
                                  << child_pid << std::endl;
                    }
                }
            }

// 2) ping actuator_srv,失败 3 次 → fork 重启
            {
                int ping_fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
                if (ping_fd >= 0) {
                    struct sockaddr_un paddr{};
                    paddr.sun_family = AF_UNIX;
                    std::strncpy(paddr.sun_path, "/tmp/car_actuator.sock",
                                 sizeof(paddr.sun_path) - 1);
                    if (connect(ping_fd, reinterpret_cast<struct sockaddr*>(&paddr),
                                sizeof(paddr)) == 0) {
                        act_ping_fail_count = 0;
                        close(ping_fd);
                    } else {
                        close(ping_fd);
                        act_ping_fail_count++;
                        if (act_ping_fail_count >= 3) {
                            std::cerr << "[car_core] watchdog: actuator_srv "
                                      << "unreachable (3 pings failed), restarting..."
                                      << std::endl;
                            // 先回收自管的旧 actuator_srv (如已退出)
                            if (act_child_pid > 0) {
                                int st = 0;
                                if (waitpid(act_child_pid, &st, WNOHANG) == act_child_pid) {
                                    act_child_pid = -1;
                                }
                            }
                            // fork + setsid + execlp 解挂父子关系,避免被 car_core 影响
                            if (act_child_pid <= 0) {
                                pid_t new_act = fork();
                                if (new_act == 0) {
                                    setsid();
                                    execlp("./actuator_srv/actuator_srv",
                                           "actuator_srv", nullptr);
                                    _exit(1);
                                } else if (new_act > 0) {
                                    act_child_pid = new_act;
                                    std::cout << "[car_core] watchdog: restarted "
                                              << "actuator_srv pid=" << act_child_pid
                                              << std::endl;
                                }
                            }
                            act_ping_fail_count = 0;  // 重置,等下次 ping 验证
                        }
                    }
                } else {
                    act_ping_fail_count++;
                }
            }

            // 3) 驾驶循环计数：每 15 秒模拟一个驾驶循环
            {
                static int cycle_secs = 0;
                if (++cycle_secs >= 15) {
                    cycle_secs = 0;
                    dtc.markDrivingCycle();
                }
            }

            // 4) 同步 actuator_srv 门控状态到共享内存 (每 1 秒)
            {
                static uint8_t last_mask = 0;
                uint8_t mask = queryDoorMask();
                if (mask != last_mask) {
                    // 写到两个 buf,让 dashboard 立刻看到状态变化
                    if (g_shm_buf[0]) g_shm_buf[0]->door_mask = mask;
                    if (g_shm_buf[1]) g_shm_buf[1]->door_mask = mask;
                    last_mask = mask;
                }
            }
        }

        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == g_can_notify_fd) {
                // CAN线程通知
                uint64_t count;
                read(g_can_notify_fd, &count, sizeof(count));
                CanData data;
                while (g_queue.pop(data)) {
                    // ── Ping-Pong 双缓冲：写非活跃 buf ──
                    uint32_t active = header->read_index.load(
                        std::memory_order_relaxed);
                    uint32_t target = 1 - active;
                    ShmBlock* wbuf = g_shm_buf[target];

                    // 更新车况数据
                    wbuf->speed_kmh       = data.speed_kmh;
                    wbuf->engine_rpm      = data.engine_rpm;
                    wbuf->water_temp_c    = data.water_temp_c;
                    wbuf->oil_temp_c      = data.oil_temp_c;
                    wbuf->fuel_percent    = data.fuel_percent;
                    wbuf->battery_voltage = data.battery_voltage;
                    wbuf->gear            = data.gear;
                    wbuf->hand_brake      = data.hand_brake;
                    wbuf->lock_status     = data.lock_status;

                    // ── DTC 诊断 ──
                    now_ms += 50;
                    dtc.update(*wbuf, now_ms);
                    wbuf->fault_lamp_mask = dtc.faultLampMask();
                    dtc.fillBlinkModes(wbuf->fault_blink);

                    // ── 推入 DB 线程队列 (异步写 SQLite) ──
                    for (const auto& rec : dtc.activeDtcs()) {
                        DbEntry entry{};
                        entry.code = rec.code;
                        entry.severity = rec.severity;
                        entry.confirmed_ms = rec.confirmed_ms;
                        std::strncpy(entry.text, rec.toString().c_str(),
                                     sizeof(entry.text) - 1);
                        entry.text[sizeof(entry.text) - 1] = '\0';
                        entry.freeze = rec.freeze;
                        g_db_queue.push(entry);
                    }

                    // ── 业务规则：自动落锁 + 超速告警 ──
                    if (data.speed_kmh > 20.0f && data.lock_status == 0) {  // 自动落锁阈值按设计文档 20km/h
                        auto lockReq = makeReq(ModId::DOOR, CmdType::WRITE,
                                               door_item::kLock, 1);
                        int lockFd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
                        struct sockaddr_un laddr{};
                        laddr.sun_family = AF_UNIX;
                        std::strncpy(laddr.sun_path, "/tmp/car_actuator.sock",
                                     sizeof(laddr.sun_path)-1);
                        if (::connect(lockFd, (struct sockaddr*)&laddr, sizeof(laddr)) == 0) {
                            ::send(lockFd, &lockReq, sizeof(lockReq), MSG_NOSIGNAL);
                            ::close(lockFd);
                        }
                    }
                    if (data.speed_kmh > 120.0f) {
                        static int warnCnt = 0;
                        if (++warnCnt % 20 == 0)
                            std::cerr << "[car_core] SPEED WARNING: "
                                      << data.speed_kmh << " km/h" << std::endl;
                    }

                    // ── 原子切换：写端完成，切 read_index 指向新 buf ──
                    std::atomic_thread_fence(std::memory_order_release);
                    header->read_index.store(target,
                        std::memory_order_release);

                    // 每帧 bump version, 供 car_dashboard 活跃性检测
                    header->version.fetch_add(1, std::memory_order_release);

                    // 通知 Qt
                    uint64_t one = 1;
                    write(g_qt_notify_fd, &one, sizeof(one));
                }
            } else if (events[i].data.fd == g_listen_fd) {
                // Unix socket 客户端连接
                int client_fd = accept(g_listen_fd, nullptr, nullptr);
                if (client_fd >= 0) {
                    handle_car_core_client(client_fd);
                }
            }
        }
    }
    // 清理资源
    if (child_pid > 0) {
        kill(child_pid, SIGTERM);
        waitpid(child_pid, nullptr, 0);
    }
    can_thread.join();
    // 通知 DB 线程退出 + 等待排空
    g_db_running.store(false, std::memory_order_release);
    if (g_db_thread.joinable()) g_db_thread.join();
    close(g_can_notify_fd);
    close(g_qt_notify_fd);
    close(epoll_fd);
    if (g_listen_fd >= 0) { close(g_listen_fd); unlink(kSockPath); }
    shmdt(raw);
    shmctl(g_shmid, IPC_RMID, nullptr);
    return 0;
}