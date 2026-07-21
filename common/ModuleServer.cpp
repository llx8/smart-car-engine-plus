#include "ModuleServer.hpp"
#include "CarMsg.hpp"

#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <csignal>

namespace {
    volatile std::sig_atomic_t g_server_running = 1;
    void handle_signal(int) { g_server_running = 0; }
}

ModuleServer::ModuleServer(const char* socket_path)
    : sock_path_(socket_path)
{
    createSocket();
    std::signal(SIGINT, handle_signal);
    std::signal(SIGTERM, handle_signal);
}

ModuleServer::~ModuleServer()
{
    cleanup();
}

void ModuleServer::createSocket()
{
    // 1. 创建 socket
    listen_fd_ = socket(AF_UNIX, SOCK_SEQPACKET, 0);
    if (listen_fd_ < 0) {
        std::cerr << "[ModuleServer] socket() failed" << std::endl;
        return;
    }

    // 2. 清理旧 socket 文件
    unlink(sock_path_.c_str());

    // 3. bind
    struct sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, sock_path_.c_str(), sizeof(addr.sun_path) - 1);

    if (bind(listen_fd_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) < 0) {
        std::cerr << "[ModuleServer] bind(" << sock_path_ << ") failed" << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    // 4. listen
    if (listen(listen_fd_, 5) < 0) {
        std::cerr << "[ModuleServer] listen() failed" << std::endl;
        close(listen_fd_);
        listen_fd_ = -1;
        return;
    }

    // 5. 创建 epoll
    epoll_fd_ = epoll_create1(0);
    struct epoll_event ev{};
    ev.events   = EPOLLIN;
    ev.data.fd  = listen_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &ev);

    std::cout << "[ModuleServer] listening on " << sock_path_ << std::endl;
}

void ModuleServer::run()
{
    if (listen_fd_ < 0) return;

    constexpr int kMaxEvents = 16;
    struct epoll_event events[kMaxEvents];

    while (g_server_running) {
        int nfds = epoll_wait(epoll_fd_, events, kMaxEvents, 1000);
        for (int i = 0; i < nfds; ++i) {
            if (events[i].data.fd == listen_fd_) {
                handleAccept();
            }
        }
    }
}

void ModuleServer::handleAccept()
{
    int client_fd = accept(listen_fd_, nullptr, nullptr);
    if (client_fd < 0) return;

    // 读请求（SOCK_SEQPACKET 保留消息边界，一次 recv 拿到完整请求）
    CarMsgReq req{};
    ssize_t n = recv(client_fd, &req, sizeof(req), 0);
    if (n != sizeof(req)) {
        close(client_fd);
        return;
    }

    // 校验 magic
    if (req.magic != kCarMsgMagic) {
        CarMsgResp resp = makeResp(ResultCode::ERR);
        sendResponse(client_fd, resp);
        close(client_fd);
        return;
    }

    // 分派给子类处理
    CarMsgResp resp = onRequest(req);
    sendResponse(client_fd, resp);
    close(client_fd);
}

bool ModuleServer::sendResponse(int client_fd, const CarMsgResp& resp)
{
    ssize_t n = send(client_fd, &resp, sizeof(resp), MSG_NOSIGNAL);
    return n == sizeof(resp);
}

void ModuleServer::cleanup()
{
    if (listen_fd_ >= 0) {
        close(listen_fd_);
        listen_fd_ = -1;
    }
    if (epoll_fd_ >= 0) {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
    unlink(sock_path_.c_str());
}
