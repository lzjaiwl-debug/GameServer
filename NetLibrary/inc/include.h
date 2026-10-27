#ifndef _NetLibrary_include_h_
#define _NetLibrary_include_h_

#include <NetLibrary/NetLibrary.h>
#include <Core/Core.h>
#include <Core/SSL/SSL.h>

#if defined(PLATFORM_POSIX)
    #include <ncurses.h>
    #include <arpa/inet.h>
    #include <sys/timerfd.h>
    #include <sys/epoll.h>
    #include <sys/eventfd.h>
    #include <sys/ioctl.h>
    #include <netinet/in.h>
#else
    // Windows 下此处不再包含 winsock.h，已在前面使用 winsock2.h
    #include <mswsock.h>
#endif

#include <fcntl.h>
using namespace Upp;
#include "EventEmitter.hpp"
#include "Type.h"
#include "Exception.hpp"
#include "NetSSL.h"
#include "Packet.hpp"

namespace NetLibrary {
    inline One<SockPacket>& NullSockPacket()
    {
        thread_local One<SockPacket> s;
        return s;
    }
}
#define PACKNULL NetLibrary::NullSockPacket()

#include "proHeader.hpp"

#if defined(PLATFORM_WIN32)
namespace NetLibrary {
    typedef HANDLE EPOLL;
    struct epoll_event {
        uint32_t events;
        struct {
            void* ptr;
            SOCKET fd;
            uint64_t u64;
        } data;
    };
    #ifndef EPOLLIN
    #define EPOLLIN  0x0001
    #endif
    #ifndef EPOLLCONNET
    #define EPOLLCONNET  0x0002
    #endif
    #ifndef EPOLLOUT
    #define EPOLLOUT 0x0004
    #endif
    #ifndef EPOLLET
    #define EPOLLET  0x80000000
    #endif
    #ifndef EPOLLRDHUP
    #define EPOLLRDHUP 0x2000
    #endif
    #ifndef EPOLLERR
    #define EPOLLERR 0x0008
    #endif
    #ifndef EPOLLHUP
    #define EPOLLHUP 0x0010
    #endif
    #ifndef EPOLL_CTL_ADD
    #define EPOLL_CTL_ADD 1
    #define EPOLL_CTL_DEL 2
    #define EPOLL_CTL_MOD 3
    #endif
    struct IocpContext : public AssetLib::PoolParent {
        OVERLAPPED ov;
        SOCKET acceptSocket;
        One<SockPacket> pack;
        uint32_t events;
        DWORD bytesTransferred;
        IocpContext() : acceptSocket(INVALID_SOCKET), events(0), bytesTransferred(0) { ZeroMemory(&ov, sizeof(OVERLAPPED)); }
        IocpContext(size_t k) : IocpContext() { key = k; }
		virtual void Release() override
        {
            if (acceptSocket != INVALID_SOCKET)
                closesocket(acceptSocket);
            acceptSocket = INVALID_SOCKET;
            events = 0;
            bytesTransferred = 0;
            if (pack)
                pack.Clear();
            ZeroMemory(&ov, sizeof(OVERLAPPED));
        }
    };
};
#else
typedef int EPOLL;
#endif

#include "UserInfo.h"
#include "EpollServer.h"
#include "SocketBase.h"
#include "SocketFun.h"
#include "Protocol.h"
#include "TcpServer.h"
#include "TcpClient.h"
#include "HttpServer.h"
#include "NetServer.h"
using namespace NetLibrary;
#endif
