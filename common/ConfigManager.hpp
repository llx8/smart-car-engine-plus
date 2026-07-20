#pragma once

// ── 轻量级 INI 配置解析器 ──
// 功能：读 [section] + key = value，支持字符串和整数，去空格和注释（# 开头行）
// 无第三方依赖，纯 C++17 标准库实现

#include <string>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <algorithm>

class ConfigManager {
public:
    // 从文件加载配置，失败返回 false
    bool load(const std::string& path);

    // 查询：指定 section 下的 key
    std::string getString(const std::string& section,
                          const std::string& key,
                          const std::string& default_val = "") const;

    int getInt(const std::string& section,
               const std::string& key,
               int default_val = 0) const;

    bool hasSection(const std::string& section) const;

private:
    // map[section][key] = value
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> data_;

    static std::string trim(const std::string& s);
};
