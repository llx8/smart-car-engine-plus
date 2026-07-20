#pragma once

// ── 轻量级 spdlog 封装：提供进程级日志宏 ──
// 使用方式：
//   1. main() 开头调用 Logger::init("my_process")
//   2. 用 LOG_INFO("msg {}", value) 系列宏记日志
// 特性：异步 sink（非阻塞）、自动带进程名前缀、时间戳到毫秒

#include <spdlog/spdlog.h>
#include <spdlog/async.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <memory>
#include <string>

namespace Logger {

// 初始化日志器（每个进程调用一次）
inline void init(const std::string& proc_name) {
    auto sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    auto logger = std::make_shared<spdlog::logger>(proc_name, sink);
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] [%n] [%^%l%$] %v");
    logger->set_level(spdlog::level::debug);
    spdlog::set_default_logger(logger);
}

// 切换日志等级（运行时调试用）
inline void set_level(spdlog::level::level_enum lv) {
    spdlog::set_level(lv);
}

} // namespace Logger

// ── 便捷宏 ──
#define LOG_TRACE(...) SPDLOG_TRACE(__VA_ARGS__)
#define LOG_DEBUG(...) SPDLOG_DEBUG(__VA_ARGS__)
#define LOG_INFO(...)  SPDLOG_INFO(__VA_ARGS__)
#define LOG_WARN(...)  SPDLOG_WARN(__VA_ARGS__)
#define LOG_ERROR(...) SPDLOG_ERROR(__VA_ARGS__)
