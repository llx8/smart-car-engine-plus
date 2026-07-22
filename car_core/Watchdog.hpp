#pragma once

#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <atomic>
#include <string>

#include "ShmLayout.hpp"

// 监控 car_dashboard 子进程:崩溃后重建 shm + eventfd 并 fork 重启
// 同时持有 shm 全部状态(header/buf/notify_fd),main 通过 accessor 拿指针
class DashboardWatchdog {
public:
    DashboardWatchdog(key_t key, size_t shm_size, std::string dashboard_path);
    ~DashboardWatchdog();

    bool init(); // 创建 shm + eventfd + 首次 fork 子进程

    ShmHeader* header()     const { return header_; }
    ShmBlock*  buf(int i)   const { return buf_[i]; }
    int        shmid()      const { return shmid_; }
    int        qtNotifyFd() const { return qt_notify_fd_; }
    pid_t      childPid()   const { return child_pid_; }

    // 每 1 秒调用:waitpid 检查,退出时 refork
    void tick();

private:
    bool createShm();
    void destroyShm();
    bool reforkDashboard();

    key_t        key_;
    size_t       shm_size_;
    std::string  dashboard_path_;
    void*        raw_        = nullptr;
    ShmHeader*   header_     = nullptr;
    ShmBlock*    buf_[2]     = {nullptr, nullptr};
    int          shmid_      = -1;
    int          qt_notify_fd_ = -1;
    pid_t        child_pid_  = -1;
};

// 监控 actuator_srv:每 tick ping 一次,3 次连续失败后 fork 重启
// 命令行单独启动的 actuator_srv 不由本类管理
class ActuatorWatchdog {
public:
    void tick(const char* sock_path, const char* binary_path);

private:
    int  fail_count_ = 0;
    pid_t child_pid_ = -1;
};