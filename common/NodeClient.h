#pragma once
// 内部链路的客户端一侧：主动 connect + 断线自动重连（指数退避）+ 可选心跳。
// 与 NodeServer 一样，收到的帧进收件箱，业务线程 Poll() 取。
#include "Socket.h"
#include "Frame.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <thread>

namespace gs {

class NodeClient {
public:
    ~NodeClient();

    // heartbeatMs <= 0 表示不发心跳
    bool Start(const char* host, int port, int heartbeatMs = 5000);
    void Stop();

    bool Poll(Incoming& out);
    bool Send(uint16_t msgId, uint8_t flag, const void* data, size_t len);
    bool IsConnected() const { return connected_.load(); }

private:
    void Loop();

    std::string host_;
    int         port_ = 0;
    int         heartbeatMs_ = 5000;

    std::atomic<bool> running_{ false };
    std::atomic<bool> connected_{ false };
    std::thread       th_;

    mutable std::mutex fdMu_;
    SocketFd           fd_ = kInvalidFd;

    std::mutex        inboxMu_;
    std::deque<Incoming> inbox_;
};

} // namespace gs
