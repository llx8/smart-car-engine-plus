#include "ConfigManager.hpp"

bool ConfigManager::load(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) return false;

    std::string line;
    std::string current_section;

    while (std::getline(file, line)) {
        // 去前后空格
        line = trim(line);

        // 空行或注释行
        if (line.empty() || line[0] == '#' || line[0] == ';')
            continue;

        // [section]
        if (line[0] == '[') {
            auto end = line.find(']');
            if (end != std::string::npos) {
                current_section = trim(line.substr(1, end - 1));
            }
            continue;
        }

        // key = value
        auto eq = line.find('=');
        if (eq != std::string::npos) {
            std::string key = trim(line.substr(0, eq));
            std::string val = trim(line.substr(eq + 1));
            if (!current_section.empty() && !key.empty()) {
                data_[current_section][key] = val;
            }
        }
    }
    return true;
}

std::string ConfigManager::getString(const std::string& section,
                                     const std::string& key,
                                     const std::string& default_val) const {
    auto it_sec = data_.find(section);
    if (it_sec == data_.end()) return default_val;

    auto it_key = it_sec->second.find(key);
    if (it_key == it_sec->second.end()) return default_val;

    return it_key->second;
}

int ConfigManager::getInt(const std::string& section,
                          const std::string& key,
                          int default_val) const {
    std::string s = getString(section, key);
    if (s.empty()) return default_val;
    try {
        return std::stoi(s);
    } catch (...) {
        return default_val;
    }
}

bool ConfigManager::hasSection(const std::string& section) const {
    return data_.find(section) != data_.end();
}

std::string ConfigManager::trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}
