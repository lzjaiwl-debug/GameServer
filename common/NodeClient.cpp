#include "NodeClient.h"
#include "MsgDef.h"

namespace gs {

NodeClient::~NodeClient() { Stop(); }

bool NodeClient::Start(const char* host, int port, int heartbeatMs) {
    if (running_.load()) return false;
    host_ = host ? host : "127.0.0.1";
    port_ = port;
    heartbeatMs_ = heartbeatMs;
    running_.store(true);
    th_ = std::thread(&NodeClient::Loop, this);
    return true;
}

void NodeClient::Stop() {
    if (!running_.exchange(false)) return;
    {
        std::lock_guard<std::mutex> lk(fdMu_);
        TcpClose(fd_);
        fd_ = kInvalidFd;
    }
    connected_.store(false);
    if (th_.joinable()) th_.join();
}

bool NodeClient::Poll(Incoming& out) {
    std::lock_guard<std::mutex> lk(inboxMu_);
    if (inbox_.empty()) return false;
    out = std::move(inbox_.front());
    inbox_.pop_front();
    return true;
}

bool NodeClient::Send(uint16_t msgId, uint8_t flag, const void* data, size_t len) {
    SocketFd fd;
    {
        std::lock_guard<std::mutex> lk(fdMu_);
        fd = fd_;
    }
    if (fd == kInvalidFd) return false;

    std::vector<uint8_t> bytes = EncodeFrame(msgId, flag, data, len);
    int n = TcpSend(fd, bytes.data(), (int)bytes.size());
    return n > 0;
}

void NodeClient::Loop() {
    int  backoffMs = 1000;
    auto lastHb    = std::chrono::steady_clock::now();

    while (running_.load()) {
        // ---------- 未连接：尝试连接 ----------
        if (!connected_.load()) {
            SocketFd fd = TcpConnect(host_.c_str(), port_);
            if (fd == kInvalidFd) {
                std::this_thread::sleep_for(std::chrono::milliseconds(backoffMs));
                backoffMs = backoffMs >= 15000 ? 30000 : backoffMs * 2;   // 指数退避，上限 30s
                continue;
            }
            {
                std::lock_guard<std::mutex> lk(fdMu_);
                fd_ = fd;
            }
            connected_.store(true);
            backoffMs = 1000;
            lastHb    = std::chrono::steady_clock::now();

            Incoming ev{};
            ev.connId = 0;
            ev.kind   = LinkEvent::Open;
            {
                std::lock_guard<std::mutex> lk(inboxMu_);
                inbox_.emplace_back(std::move(ev));
            }
        }

        // ---------- 已连接：收包 ----------
        SocketFd fd;
        {
            std::lock_guard<std::mutex> lk(fdMu_);
            fd = fd_;
        }
        if (fd == kInvalidFd) { connected_.store(false); continue; }

        if (WaitReadable(fd, 100)) {
            uint8_t buf[64 * 1024];
            int n = TcpRecv(fd, buf, sizeof(buf));
            if (n <= 0) {
                std::lock_guard<std::mutex> lk(fdMu_);
                TcpClose(fd_);
                fd_ = kInvalidFd;
                connected_.store(false);
                Incoming ev{}; ev.connId = 0; ev.kind = LinkEvent::Close;
                {
                    std::lock_guard<std::mutex> lk2(inboxMu_);
                    inbox_.emplace_back(std::move(ev));
                }
                continue;
            }

            static thread_local FrameDecoder dec;
            dec.Feed(buf, (size_t)n);
            Frame f;
            while (dec.Next(f)) {
                Incoming ev{};
                ev.connId = 0;
                ev.kind   = LinkEvent::Frame;
                ev.frame  = std::move(f);
                std::lock_guard<std::mutex> lk2(inboxMu_);
                inbox_.emplace_back(std::move(ev));
            }
        }

        // ---------- 心跳 ----------
        if (heartbeatMs_ > 0) {
            auto now = std::chrono::steady_clock::now();
            if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastHb).count() >= heartbeatMs_) {
                MsgHeartbeat hb{};
                std::vector<uint8_t> body;
                if (PackTo(hb, body)) Send(MsgId::HEARTBEAT, FF_NONE, body.data(), body.size());
                lastHb = now;
            }
        }
    }

    std::lock_guard<std::mutex> lk(fdMu_);
    TcpClose(fd_);
    fd_ = kInvalidFd;
    connected_.store(false);
}

} // namespace gs
