#pragma once

#include <string>
#include <functional>
#include <cstdint>

struct CarMsgReq;
struct CarMsgResp;

// ── Unix socket 服务端基类 ──
// 封装 socket 创建、epoll 事件循环、连接管理。
// 子类只需重写 onRequest()，基类负责收发和事件分发。

class ModuleServer {
public:
    explicit ModuleServer(const char* socket_path);
    virtual ~ModuleServer();

    // 阻塞运行，直到外部信号中断
    void run();

    // 发送响应给指定客户端
    static bool sendResponse(int client_fd, const CarMsgResp& resp);

protected:
    // 子类实现：处理一个请求，返回响应
    virtual CarMsgResp onRequest(const CarMsgReq& req) = 0;

private:
    std::string sock_path_;
    int listen_fd_  = -1;
    int epoll_fd_   = -1;

    void createSocket();
    void handleAccept();
    void handleClient(int client_fd);
    void cleanup();
};
