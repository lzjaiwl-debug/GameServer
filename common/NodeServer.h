#pragma once
// 内部链路的服务端一侧：listen + accept + 每连接一个收包线程。
// 收到的帧不直接回调，而是投进「收件箱」，由业务线程单线程 Poll() 取走 ——
// 这样逻辑层永远是单线程，不用到处加锁。
#include "Socket.h"
#include "Frame.h"
#include <atomic>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace gs {

enum class LinkEvent : int {
    Frame = 0,   // 收到一帧
    Open  = 1,   // 连接建立
    Close = 2,   // 连接断开
};

struct Incoming {
    uint32_t  connId = 0;
    LinkEvent kind   = LinkEvent::Frame;
    Frame     frame;
};

class NodeServer {
public:
    ~NodeServer();

    // 返回 false 表示监听失败
    bool Start(const char* host, int port);
    void Stop();
    int  Port() const { return port_; }

    // 非阻塞取一条事件；无事件返回 false
    bool Poll(Incoming& out);

    bool Send(uint32_t connId, uint16_t msgId, uint8_t flag, const void* data, size_t len);
    void Broadcast(uint16_t msgId, uint8_t flag, const void* data, size_t len);
    void CloseConn(uint32_t connId);
    size_t ConnCount() const;

private:
    struct Session {
        uint32_t              id    = 0;
        SocketFd              fd    = kInvalidFd;
        std::atomic<bool>     alive{ false };
        std::mutex            sendMu;
    };
    using SessionPtr = std::shared_ptr<Session>;

    void AcceptLoop();
    void RecvLoop(SessionPtr s);
    void Push(Incoming&& ev);

    SocketFd          listenFd_ = kInvalidFd;
    int               port_ = 0;
    std::atomic<bool> running_{ false };
    std::thread       acceptTh_;

    mutable std::mutex                             mu_;
    std::unordered_map<uint32_t, SessionPtr>       sessions_;
    uint32_t                                       nextId_ = 1;

    std::mutex        inboxMu_;
    std::deque<Incoming> inbox_;
};

} // namespace gs
