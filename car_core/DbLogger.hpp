#pragma once

#include <atomic>
#include <thread>
#include <cstdint>
#include <string>

#include "DtcEngine.hpp"
#include "ShmLayout.hpp"

// DB 落盘条目:从 DtcRecord 提取的字段 + 冻结帧快照
struct DbEntry {
    uint32_t   code;
    uint8_t    severity;
    char       text[16];
    uint64_t   confirmed_ms;
    FreezeFrame freeze;
};

class DbLogger {
public:
    explicit DbLogger(const char* db_path);
    ~DbLogger();

    // 主线程调用:把一条 DTC 推入异步写队列
    void push(const DbEntry& entry);

    // 提前退出(dtor 会调,显式调可控制时机)
    void shutdown();

private:
    void threadFunc();

    SPSCQueue<DbEntry, 64> queue_;
    std::atomic<bool> running_{true};
    std::thread thread_;
    std::string db_path_;
};