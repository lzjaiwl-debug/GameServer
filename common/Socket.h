#pragma once
// 跨平台 TCP 薄封装：只做「连接/收发/关闭」，不含任何协议逻辑。
// Windows: WinSock2   其他: BSD socket
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
namespace gs { using SocketFd = SOCKET; constexpr SocketFd kInvalidFd = INVALID_SOCKET; }
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
namespace gs { using SocketFd = int; constexpr SocketFd kInvalidFd = -1; }
#endif

namespace gs {

// 全局初始化（WSAStartup），进程内幂等；非 Windows 为空实现
bool NetInit();
void NetCleanup();

// 监听。host 传 "0.0.0.0" 监听全部网卡，port 为 0 时由系统分配（用 ListenPort 取回）
SocketFd TcpListen(const char* host, int port);
int      ListenPort(SocketFd listenFd);

// 阻塞接受（调用前应用 select 确认可读，便于响应停止信号）
SocketFd TcpAccept(SocketFd listenFd, std::string* peerIp = nullptr, int* peerPort = nullptr);

// 主动连接，失败返回 kInvalidFd
SocketFd TcpConnect(const char* host, int port);

// 返回值：>0 实际字节数；0 对端关闭；<0 出错
int  TcpSend(SocketFd fd, const void* buf, int len);
int  TcpRecv(SocketFd fd, void* buf, int len);
void TcpClose(SocketFd fd);

// 仅 Windows 有意义：判断是否为「断开」类错误
bool IsDisconnectError(int err);

// 监听 fd 是否在 timeoutMs 内可读（用于可中断的 accept 循环）
bool WaitReadable(SocketFd fd, int timeoutMs);

} // namespace gs
