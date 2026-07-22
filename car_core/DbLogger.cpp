#include "DbLogger.hpp"

#include <sqlite3.h>
#include <unistd.h>

#include <iostream>

DbLogger::DbLogger(const char* db_path) : db_path_(db_path) {
    thread_ = std::thread(&DbLogger::threadFunc, this);
}

DbLogger::~DbLogger() { shutdown(); }

void DbLogger::push(const DbEntry& entry) { queue_.push(entry); }

void DbLogger::shutdown() {
    if (!running_.exchange(false)) return;
    if (thread_.joinable()) thread_.join();
}

void DbLogger::threadFunc() {
    sqlite3* db = nullptr;
    if (sqlite3_open(db_path_.c_str(), &db) != SQLITE_OK) {
        std::cerr << "[DbLogger] sqlite3_open(" << db_path_
                  << ") failed: " << (db ? sqlite3_errmsg(db) : "null ptr")
                  << " — DTC 记录将不会落盘" << std::endl;
        if (db) sqlite3_close(db);
        return;
    }

    const char* ddl =
        "CREATE TABLE IF NOT EXISTS dtc_log ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  dtc_code INTEGER, dtc_text TEXT, severity INTEGER,"
        "  first_seen INTEGER, confirmed_at INTEGER, active INTEGER,"
        "  speed REAL, rpm INTEGER, water_temp REAL, oil_temp REAL,"
        "  fuel REAL, battery REAL);";
    sqlite3_exec(db, ddl, nullptr, nullptr, nullptr);

    const char* ins =
        "INSERT INTO dtc_log "
        "(dtc_code, dtc_text, severity, first_seen, confirmed_at, active,"
        " speed, rpm, water_temp, oil_temp, fuel, battery)"
        " VALUES (?,?,?,?,?,1, ?,?,?,?,?,?);";

    auto writeOne = [&](const DbEntry& e) {
        sqlite3_stmt* stmt = nullptr;
        if (sqlite3_prepare_v2(db, ins, -1, &stmt, nullptr) != SQLITE_OK) return;
        sqlite3_bind_int(stmt, 1, static_cast<int>(e.code));
        sqlite3_bind_text(stmt, 2, e.text, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, e.severity);
        sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(e.freeze.timestamp_ms));
        sqlite3_bind_int64(stmt, 5, static_cast<sqlite3_int64>(e.confirmed_ms));
        sqlite3_bind_double(stmt, 6, e.freeze.speed_kmh);
        sqlite3_bind_int(stmt, 7, e.freeze.engine_rpm);
        sqlite3_bind_double(stmt, 8, e.freeze.water_temp_c);
        sqlite3_bind_double(stmt, 9, e.freeze.oil_temp_c);
        sqlite3_bind_double(stmt, 10, e.freeze.fuel_percent);
        sqlite3_bind_double(stmt, 11, e.freeze.battery_voltage);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    };

    while (running_.load(std::memory_order_acquire)) {
        DbEntry entry;
        if (!queue_.pop(entry)) {
            usleep(5000);
            continue;
        }
        writeOne(entry);
    }
    // 排空队列里剩余数据后再关闭
    DbEntry entry;
    while (queue_.pop(entry)) writeOne(entry);
    sqlite3_close(db);
}