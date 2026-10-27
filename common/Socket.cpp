#include "Socket.h"
#include <cstring>
#include <mutex>

namespace gs {

#ifdef _WIN32
namespace {
    std::once_flag g_initOnce;
    bool           g_inited = false;
}
bool NetInit() {
    std::call_once(g_initOnce, []() {
        WSADATA wd{};
        g_inited = (::WSAStartup(MAKEWORD(2, 2), &wd) == 0);
    });
    return g_inited;
}
void NetCleanup() {
    if (g_inited) { ::WSACleanup(); g_inited = false; }
}
static int LastErr() { return ::WSAGetLastError(); }
bool IsDisconnectError(int err) {
    return err == WSAECONNRESET || err == WSAECONNABORTED
        || err == WSAESHUTDOWN  || err == WSAETIMEDOUT
        || err == WSAENOTCONN   || err == WSAEHOSTUNREACH
        || err == WSAENETDOWN   || err == WSAENETRESET;
}
void TcpClose(SocketFd fd) {
    if (fd == kInvalidFd) return;
    ::shutdown(fd, SD_BOTH);
    ::closesocket(fd);
}
#else
bool NetInit()    { return true; }
void NetCleanup() {}
static int LastErr() { return errno; }
bool IsDisconnectError(int err) {
    return err == ECONNRESET || err == EPIPE || err == ETIMEDOUT || err == ENOTCONN || err == EHOSTUNREACH;
}
void TcpClose(SocketFd fd) {
    if (fd == kInvalidFd) return;
    ::shutdown(fd, SHUT_RDWR);
    ::close(fd);
}
#endif

SocketFd TcpListen(const char* host, int port) {
    if (!NetInit()) return kInvalidFd;
    SocketFd fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidFd) return kInvalidFd;

    int on = 1;
    ::setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, (const char*)&on, sizeof(on));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    if (host && *host && std::strcmp(host, "0.0.0.0") != 0)
        addr.sin_addr.s_addr = ::inet_addr(host);
    else
        addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (::bind(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { TcpClose(fd); return kInvalidFd; }
    if (::listen(fd, 64) != 0) { TcpClose(fd); return kInvalidFd; }
    return fd;
}

int ListenPort(SocketFd listenFd) {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (::getsockname(listenFd, (sockaddr*)&addr, &len) != 0) return 0;
    return ntohs(addr.sin_port);
}

SocketFd TcpAccept(SocketFd listenFd, std::string* peerIp, int* peerPort) {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    SocketFd cfd = ::accept(listenFd, (sockaddr*)&addr, &len);
    if (cfd == kInvalidFd) return kInvalidFd;
    if (peerIp) {
        char ipbuf[64]{};
        const char* p = ::inet_ntop(AF_INET, &addr.sin_addr, ipbuf, sizeof(ipbuf));
        *peerIp = p ? p : "";
    }
    if (peerPort) *peerPort = ntohs(addr.sin_port);
    return cfd;
}

SocketFd TcpConnect(const char* host, int port) {
    if (!NetInit()) return kInvalidFd;
    SocketFd fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidFd) return kInvalidFd;

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((unsigned short)port);
    addr.sin_addr.s_addr = ::inet_addr(host ? host : "127.0.0.1");

    if (::connect(fd, (sockaddr*)&addr, sizeof(addr)) != 0) { TcpClose(fd); return kInvalidFd; }
    return fd;
}

int TcpSend(SocketFd fd, const void* buf, int len) {
    const char* p = (const char*)buf;
    int sent = 0;
    while (sent < len) {
        int n = ::send(fd, p + sent, len - sent, 0);
        if (n <= 0) return n < 0 ? n : sent;
        sent += n;
    }
    return sent;
}

int TcpRecv(SocketFd fd, void* buf, int len) {
    return (int)::recv(fd, (char*)buf, len, 0);
}

bool WaitReadable(SocketFd fd, int timeoutMs) {
    if (fd == kInvalidFd) return false;
    fd_set rs; FD_ZERO(&rs); FD_SET(fd, &rs);
    timeval tv{};
    tv.tv_sec  = timeoutMs / 1000;
    tv.tv_usec = (timeoutMs % 1000) * 1000;
    int r = ::select((int)fd + 1, &rs, nullptr, nullptr, &tv);
    return r > 0;
}

} // namespace gs
