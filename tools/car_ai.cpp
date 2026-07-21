#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include <chrono>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

#include "CarMsg.hpp"
#include "ConfigManager.hpp"

using json = nlohmann::json;

// ── 配置（默认值，会从 conf/car_ai.conf 覆盖） ──
static std::string g_apiKey   = "";
static std::string g_apiUrl   = "https://api.deepseek.com/v1/chat/completions";
static std::string g_model    = "deepseek-chat";
static std::string g_coreSock = "/tmp/car_core.sock";
static int         g_timeout  = 3;
static int         g_maxRetry = 1;

// ── 从配置文件加载 ──
static void loadConfig() {
    ConfigManager cfg;
    if (!cfg.load("../conf/car_ai.conf")) {
        if (!cfg.load("conf/car_ai.conf")) {
            std::cerr << "[car_ai] 找不到配置文件，使用默认值" << std::endl;
            return;
        }
    }

    // 优先读环境变量，其次读配置文件
    const char* envKey = std::getenv("DEEPSEEK_API_KEY");
    g_apiKey = envKey ? envKey : cfg.getString("api", "key");
    g_apiUrl   = cfg.getString("api", "url", g_apiUrl);
    g_model    = cfg.getString("api", "model", g_model);
    g_timeout  = cfg.getInt("api", "timeout_ms", 3000) / 1000;
    g_maxRetry = cfg.getInt("api", "max_retries", 1);
    g_coreSock = cfg.getString("uds", "socket_path", g_coreSock);

    if (g_timeout < 1) g_timeout = 3;

    std::cout << "[car_ai] 配置加载完成" << std::endl;
    std::cout << "  API URL: " << g_apiUrl << std::endl;
    std::cout << "  Model:   " << g_model << std::endl;
    std::cout << "  Timeout: " << g_timeout << "s" << std::endl;
}

// ── system prompt：定义可控字段 + 安全规则 ──
static const char* kSystemPrompt = R"(
你是一个车载语音助手的后端。用户用自然语言发出车辆控制指令。
你需要返回一个 JSON 对象，格式为：
{"actions":[{"module":"door","field":"<字段名>","value":<数值>}, ...]}

可控字段:
  门控 (module="door"):
    field: "left_front"  (左前窗, 0=关 1=开)
    field: "right_front" (右前窗)
    field: "left_rear"   (左后窗)
    field: "right_rear"  (右后窗)
    field: "trunk"       (后备箱)
    field: "lock"        (中控锁)

  空调 (module="ac"):
    field: "ac_switch"   (AC开关, 0=关 1=开)
    field: "fan_speed"   (风量, 0-7)
    field: "target_temp" (设定温度, 16-32)
    field: "recirc"      (内外循环, 0=外 1=内)

安全规则（绝对不可违反）:
  - 禁止操作档位 (gear) 相关指令
  - 禁止在车速 > 5km/h 时开车门或车窗
  - 风量范围 0-7，温度范围 16-32
  - 中控锁是 lock，不是其他名字

请只返回 JSON，不要加任何解释文字。
)";

// ── 字段名到 item_id 的映射 ──
struct FieldMapping {
    const char* name;
    ModId mod;
    uint8_t item;
};

static const FieldMapping kFieldMap[] = {
    {"left_front",  ModId::DOOR, door_item::kWindowFL},
    {"right_front", ModId::DOOR, door_item::kWindowFR},
    {"left_rear",   ModId::DOOR, door_item::kWindowRL},
    {"right_rear",  ModId::DOOR, door_item::kWindowRR},
    {"trunk",       ModId::DOOR, door_item::kTrunk},
    {"lock",        ModId::DOOR, door_item::kLock},
    {"ac_switch",   ModId::AC,   ac_item::kAcSwitch},
    {"fan_speed",   ModId::AC,   ac_item::kFanSpeed},
    {"target_temp", ModId::AC,   ac_item::kTargetTemp},
    {"recirc",      ModId::AC,   ac_item::kRecirculation},
    {nullptr, ModId::DOOR, 0}
};

static bool findField(const std::string& name, ModId& mod, uint8_t& item) {
    for (int i = 0; kFieldMap[i].name; ++i) {
        if (name == kFieldMap[i].name) {
            mod  = kFieldMap[i].mod;
            item = kFieldMap[i].item;
            return true;
        }
    }
    return false;
}

// ── libcurl 回调：收集响应 ──
static size_t writeCallback(void* contents, size_t size, size_t nmemb, std::string* out) {
    size_t total = size * nmemb;
    out->append(static_cast<char*>(contents), total);
    return total;
}

// ── 调用 DeepSeek API ──
// request_id: 幂等性标识，首次调用传空字符串，重试时复用同一 id
static std::string callDeepSeek(const std::string& userMsg, std::string& request_id) {
    CURL* curl = curl_easy_init();
    if (!curl) return "";

    json body;
    body["model"] = g_model;
    body["messages"] = json::array({
        {{"role", "system"}, {"content", kSystemPrompt}},
        {{"role", "user"},   {"content", userMsg}}
    });
    body["temperature"] = 0.1;
    // 幂等性: 重试时复用同一 request_id
    if (request_id.empty()) {
        request_id = std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count());
    }
    body["metadata"] = {{"request_id", request_id}};
    std::string bodyStr = body.dump();

    std::string response;
    struct curl_slist* headers = nullptr;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    std::string auth = std::string("Authorization: Bearer ") + g_apiKey;
    headers = curl_slist_append(headers, auth.c_str());

    curl_easy_setopt(curl, CURLOPT_URL, g_apiUrl.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, bodyStr.c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, g_timeout);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);

    if (res != CURLE_OK) return "";
    if (http_code >= 400) {  // HTTP 错误也触发重试
        std::cerr << "  DeepSeek HTTP " << http_code << std::endl;
        return "";
    }
    return response;
}

// ── 通过 Unix socket 发送指令到 car_core ──
// 返回 ResultCode 原始值, 0=OK
static uint8_t sendCommand(ModId mod, uint8_t item, uint8_t value) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return static_cast<uint8_t>(ResultCode::ERR);

    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, g_coreSock.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd); return static_cast<uint8_t>(ResultCode::ERR);
    }

    auto req = makeReq(mod, CmdType::WRITE, item, value);
    send(fd, &req, sizeof(req), MSG_NOSIGNAL);

    CarMsgResp resp{};
    recv(fd, &resp, sizeof(resp), 0);
    close(fd);

    return resp.result;
}

static const char* resultCodeStr(uint8_t code) {
    switch (static_cast<ResultCode>(code)) {
    case ResultCode::OK:           return "OK";
    case ResultCode::ERR:          return "ERR";
    case ResultCode::UNKNOWN_CMD:  return "UNKNOWN_CMD";
    case ResultCode::UNKNOWN_MOD:  return "UNKNOWN_MOD";
    case ResultCode::UNKNOWN_ITEM: return "UNKNOWN_ITEM";
    case ResultCode::OUT_OF_RANGE: return "OUT_OF_RANGE";
    case ResultCode::SAFETY_BLOCK: return "SAFETY_BLOCK (行驶中安全拦截)";
    default:                       return "UNKNOWN";
    }
}

// ── 安全校验（纵深防御第一道） ──
static bool isSafetyAllowed(ModId mod, uint8_t item) {
    // 档位禁止 AI 控制
    (void)mod; (void)item;
    // 所有字段均在 kFieldMap 中，不含 gear 相关项 → 此处只需声明策略
    // 行驶中车门/车窗拦截由 car_core SAFETY_BLOCK 做第二道防线
    return true;
}

// ── 通知 car_core 更新 ai_status（网络降级/恢复） ──
static void notifyAiStatus(int32_t status, const char* msg) {
    int fd = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (fd < 0) return;
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, g_coreSock.c_str(), sizeof(addr.sun_path) - 1);
    if (connect(fd, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(fd); return;
    }
    CarMsgReq req = makeReq(ModId::AI, CmdType::WRITE, ai_item::kStatus,
                            static_cast<uint8_t>(status));
    send(fd, &req, sizeof(req), MSG_NOSIGNAL);
    CarMsgResp resp{};
    recv(fd, &resp, sizeof(resp), 0);
    close(fd);
    (void)msg; // ai_message 暂时只设 status 位,Qt 端根据 status 显示固定提示
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);
    loadConfig();

    std::cout << "=== 车载 AI 语音助手 ===" << std::endl;
    std::cout << "输入自然语言指令（如 '打开左前窗'、'空调调到24度'），输入 quit 退出"
              << std::endl;

    bool ai_degraded = false;
    std::string input;
    while (true) {
        std::cout << "\n> ";
        std::getline(std::cin, input);
        if (std::cin.eof()) break;    // 处理 stdin 关闭(管道/文件输入),避免死循环
        if (input.empty()) continue;
        if (input == "quit" || input == "exit") break;

        // 调用 DeepSeek（带重试，同一 request_id 幂等）
        std::string jsonResp;
        std::string req_id;
        for (int retry = 0; retry <= g_maxRetry; ++retry) {
            jsonResp = callDeepSeek(input, req_id);
            if (!jsonResp.empty()) break;
            if (retry < g_maxRetry) {
                std::cout << "  [retry " << (retry + 1) << "/" << g_maxRetry
                          << "]" << std::endl;
            }
        }

        if (jsonResp.empty()) {
            if (!ai_degraded) {
                ai_degraded = true;
                notifyAiStatus(1, "AI 服务不可用，请稍后重试");
            }
            std::cerr << "  AI 服务不可用，请稍后重试" << std::endl;
            continue;
        }
        if (ai_degraded) {
            ai_degraded = false;
            notifyAiStatus(0, "");
        }

        // 解析 JSON 响应
        try {
            auto resp = json::parse(jsonResp);
            std::string content = resp["choices"][0]["message"]["content"];
            // 去掉可能的 markdown 代码块标记
            if (content.find("```") != std::string::npos) {
                auto start = content.find('{');
                auto end   = content.rfind('}');
                if (start != std::string::npos && end != std::string::npos)
                    content = content.substr(start, end - start + 1);
            }

            auto parsed = json::parse(content);
            if (!parsed.contains("actions")) {
                std::cout << "  [AI] " << content << std::endl;
                continue;
            }

            int ok = 0, fail = 0;
            for (const auto& action : parsed["actions"]) {
                std::string field = action["field"];
                int value = action["value"];

                ModId mod;
                uint8_t item;
                if (!findField(field, mod, item)) {
                    std::cerr << "  未知字段: " << field << std::endl;
                    fail++;
                    continue;
                }
                if (!isSafetyAllowed(mod, item)) {
                    std::cerr << "  安全拦截: " << field << std::endl;
                    fail++;
                    continue;
                }

                uint8_t rc = sendCommand(mod, item, static_cast<uint8_t>(value));
                if (rc == static_cast<uint8_t>(ResultCode::OK)) {
                    std::cout << "  OK " << field << " = " << value << std::endl;
                    ok++;
                } else {
                    std::cerr << "  FAIL " << field << " -> " << resultCodeStr(rc)
                              << std::endl;
                    fail++;
                }
            }
            std::cout << "  完成: " << ok << " 成功, " << fail << " 失败" << std::endl;

        } catch (const std::exception& e) {
            std::cerr << "  AI 响应解析失败: " << e.what() << std::endl;
            std::cerr << "  原始响应: " << jsonResp.substr(0, 200) << std::endl;
        }
    }

    curl_global_cleanup();
    return 0;
}
