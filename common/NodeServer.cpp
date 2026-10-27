#include "NodeServer.h"
#include <chrono>

namespace gs {

NodeServer::~NodeServer() { Stop(); }

bool NodeServer::Start(const char* host, int port) {
    if (running_.load()) return false;
    listenFd_ = TcpListen(host, port);
    if (listenFd_ == kInvalidFd) return false;
    port_ = ListenPort(listenFd_);
    if (port_ == 0) port_ = port;

    running_.store(true);
    acceptTh_ = std::thread(&NodeServer::AcceptLoop, this);
    return true;
}

void NodeServer::Stop() {
    if (!running_.exchange(false)) return;

    SocketFd lfd = listenFd_;
    listenFd_ = kInvalidFd;
    TcpClose(lfd);

    if (acceptTh_.joinable()) acceptTh_.join();

    std::vector<SessionPtr> all;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& kv : sessions_) all.push_back(kv.second);
        sessions_.clear();
    }
    for (auto& s : all) {
        s->alive.store(false);
        TcpClose(s->fd);            // 收包线程会因此退出
    }
}

bool NodeServer::Poll(Incoming& out) {
    std::lock_guard<std::mutex> lk(inboxMu_);
    if (inbox_.empty()) return false;
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}

void NodeServer::Push(Incoming&& ev) {
    std::lock_guard<std::mutex> lk(inboxMu_);
    inbox_.emplace_back(std::move(ev));
}

bool NodeServer::Send(uint32_t connId, uint16_t msgId, uint8_t flag, const void* data, size_t len) {
    SessionPtr s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sessions_.find(connId);
        if (it == sessions_.end()) return false;
        s = it->second;
    }
    if (!s->alive.load()) return false;

    std::vector<uint8_t> bytes = EncodeFrame(msgId, flag, data, len);
    std::lock_guard<std::mutex> lk(s->sendMu);
    int n = TcpSend(s->fd, bytes.data(), (int)bytes.size());
    if (n <= 0) {
        s->alive.store(false);
        return false;
    }
    return true;
}

void NodeServer::Broadcast(uint16_t msgId, uint8_t flag, const void* data, size_t len) {
    std::vector<SessionPtr> all;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& kv : sessions_) all.push_back(kv.second);
    }
    std::vector<uint8_t> bytes = EncodeFrame(msgId, flag, data, len);
    for (auto& s : all) {
        if (!s->alive.load()) continue;
        std::lock_guard<std::mutex> lk(s->sendMu);
        if (TcpSend(s->fd, bytes.data(), (int)bytes.size()) <= 0) s->alive.store(false);
    }
}

void NodeServer::CloseConn(uint32_t connId) {
    SessionPtr s;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = sessions_.find(connId);
        if (it == sessions_.end()) return;
        s = it->second;
        sessions_.erase(it);
    }
    s->alive.store(false);
    TcpClose(s->fd);
}

size_t NodeServer::ConnCount() const {
    std::lock_guard<std::mutex> lk(mu_);
    return sessions_.size();
}

void NodeServer::AcceptLoop() {
    while (running_.load()) {
        SocketFd lfd = listenFd_;
        if (lfd == kInvalidFd) break;
        if (!WaitReadable(lfd, 200)) continue;   // 200ms 一轮，便于响应 Stop()

        std::string peer;
        int         peerPort = 0;
        SocketFd    cfd = TcpAccept(lfd, &peer, &peerPort);
        if (cfd == kInvalidFd) continue;

        auto s = std::make_shared<Session>();
        s->fd    = cfd;
        s->alive.store(true);
        {
            std::lock_guard<std::mutex> lk(mu_);
            s->id = nextId_++;
            sessions_[s->id] = s;
        }

        Incoming ev{};
        ev.connId = s->id;
        ev.kind   = LinkEvent::Open;
        Push(std::move(ev));

        std::thread(&NodeServer::RecvLoop, this, s).detach();
    }
}

void NodeServer::RecvLoop(SessionPtr s) {
    FrameDecoder dec;
    std::vector<uint8_t> buf(64 * 1024);

    while (s->alive.load() && running_.load()) {
        int n = TcpRecv(s->fd, buf.data(), (int)buf.size());
        if (n <= 0) break;
        dec.Feed(buf.data(), (size_t)n);

        Frame f;
        while (dec.Next(f)) {
            Incoming ev{};
            ev.connId = s->id;
            ev.kind   = LinkEvent::Frame;
            ev.frame  = std::move(f);
            Push(std::move(ev));
        }
    }

    bool wasAlive = s->alive.exchange(false);
    {
        std::lock_guard<std::mutex> lk(mu_);
        sessions_.erase(s->id);
    }
    if (wasAlive) {
        Incoming ev{};
        ev.connId = s->id;
        ev.kind   = LinkEvent::Close;
        Push(std::move(ev));
    }
}

} // namespace gs
