#include "Watchdog.hpp"

#include <sys/eventfd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>
#include <sys/wait.h>
#include <cstring>
#include <iostream>

// ── DashboardWatchdog ──

DashboardWatchdog::DashboardWatchdog(key_t key, size_t shm_size,
                                     std::string dashboard_path)
    : key_(key), shm_size_(shm_size), dashboard_path_(std::move(dashboard_path)) {}

DashboardWatchdog::~DashboardWatchdog() {
    if (child_pid_ > 0) {
        kill(child_pid_, SIGTERM);
        waitpid(child_pid_, nullptr, 0);
    }
    destroyShm();
    if (qt_notify_fd_ >= 0) close(qt_notify_fd_);
}

bool DashboardWatchdog::init() {
    if (!createShm()) return false;
    qt_notify_fd_ = eventfd(0, EFD_NONBLOCK);
    if (qt_notify_fd_ < 0) {
        destroyShm();
        return false;
    }
    return reforkDashboard();
}

bool DashboardWatchdog::createShm() {
    shmid_ = shmget(key_, shm_size_, IPC_CREAT | 0666);
    if (shmid_ < 0) {
        std::cerr << "[Watchdog] shmget failed" << std::endl;
        return false;
    }
    raw_ = shmat(shmid_, nullptr, 0);
    if (raw_ == reinterpret_cast<void*>(-1)) {
        raw_ = nullptr;
        std::cerr << "[Watchdog] shmat failed" << std::endl;
        return false;
    }
    // 手动对齐到 64 字节边界,防止 ARM 上 atomic 非对齐触发 Bus Error
    uintptr_t aligned = (reinterpret_cast<uintptr_t>(raw_) + 63) & ~(uintptr_t)63;
    header_       = reinterpret_cast<ShmHeader*>(aligned);
    buf_[0]       = reinterpret_cast<ShmBlock*>(aligned + sizeof(ShmHeader));
    buf_[1]       = reinterpret_cast<ShmBlock*>(aligned + sizeof(ShmHeader) + sizeof(ShmBlock));

    std::memset(header_, 0, sizeof(ShmHeader));
    std::memset(buf_[0], 0, sizeof(ShmBlock));
    std::memset(buf_[1], 0, sizeof(ShmBlock));
    header_->magic = 0xCAFE0001;
    header_->version.store(0, std::memory_order_relaxed);
    header_->read_index.store(0, std::memory_order_relaxed);
    return true;
}

void DashboardWatchdog::destroyShm() {
    if (raw_) {
        shmdt(raw_);
        raw_ = nullptr;
    }
    if (shmid_ >= 0) {
        shmctl(shmid_, IPC_RMID, nullptr);
        shmid_ = -1;
    }
    header_ = nullptr;
    buf_[0] = buf_[1] = nullptr;
}

bool DashboardWatchdog::reforkDashboard() {
    pid_t pid = fork();
    if (pid == 0) {
        char shmid_str[32], evfd_str[32];
        std::snprintf(shmid_str, sizeof(shmid_str), "%d", shmid_);
        std::snprintf(evfd_str, sizeof(evfd_str), "%d", qt_notify_fd_);
        execlp(dashboard_path_.c_str(), "car_dashboard",
               shmid_str, evfd_str, nullptr);
        std::cerr << "[Watchdog] exec car_dashboard failed" << std::endl;
        _exit(1);
    }
    if (pid > 0) {
        child_pid_ = pid;
        std::cout << "[Watchdog] car_dashboard pid=" << child_pid_ << std::endl;
        return true;
    }
    std::cerr << "[Watchdog] fork failed" << std::endl;
    return false;
}

void DashboardWatchdog::tick() {
    if (child_pid_ <= 0) return;
    int status = 0;
    if (waitpid(child_pid_, &status, WNOHANG) != child_pid_) return;

    std::cerr << "[Watchdog] car_dashboard (pid=" << child_pid_
              << ") exited, restarting..." << std::endl;

    // 重建 shm + eventfd,避免 raced 残留
    destroyShm();
    if (qt_notify_fd_ >= 0) {
        close(qt_notify_fd_);
        qt_notify_fd_ = -1;
    }
    if (!createShm()) {
        child_pid_ = -1;
        return;
    }
    qt_notify_fd_ = eventfd(0, EFD_NONBLOCK);
    if (qt_notify_fd_ < 0) {
        child_pid_ = -1;
        return;
    }
    reforkDashboard();
}

// ── ActuatorWatchdog ──

void ActuatorWatchdog::tick(const char* sock_path, const char* binary_path) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) {
        fail_count_++;
        return;
    }
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path, sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) == 0) {
        fail_count_ = 0;
        close(fd);
        return;
    }
    close(fd);

    if (++fail_count_ < 3) return;

    std::cerr << "[Watchdog] actuator_srv unreachable (3 pings failed), restarting..."
              << std::endl;
    // 回收自管理的旧实例(如果由本看门狗重启过)
    if (child_pid_ > 0) {
        int st = 0;
        if (waitpid(child_pid_, &st, WNOHANG) == child_pid_) child_pid_ = -1;
    }
    if (child_pid_ <= 0) {
        pid_t new_pid = fork();
        if (new_pid == 0) {
            setsid(); // 解挂父子关系,避免被 car_core 退出连带杀掉
            execlp(binary_path, "actuator_srv", nullptr);
            _exit(1);
        }
        if (new_pid > 0) {
            child_pid_ = new_pid;
            std::cout << "[Watchdog] restarted actuator_srv pid=" << child_pid_
                      << std::endl;
        }
    }
    fail_count_ = 0; // 重置,下个 3 次 ping 验证
}