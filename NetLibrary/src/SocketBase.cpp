#include "../inc/include.h"
#ifdef PLATFORM_WIN32
void SocketBase::PostRecvWork(IocpContext* context)
{
    // Windows-specific implementation for posting a receive work
    SOCKET s = stype == SockType::SERVER ? context->acceptSocket : Socket_Listen;
    DWORD bytesReceived = 0;
    DWORD dwFlags = 0;
    // 防御：当缓冲指针为空时不要调用WSARecv
    WSABUF* wsabuf = *context->pack;
    if (!wsabuf || wsabuf->buf == nullptr || wsabuf->len == 0) {
		assetEventBus.Emit(SYSEVENT::SYSERR, "PostRecvWork: invalid buffer");
        return;
    }
    int rc = WSARecv(s, wsabuf, 1, &bytesReceived, &dwFlags, &context->ov, NULL);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
			assetEventBus.Emit(SYSEVENT::SYSERR, ~String("PostRecvWork Function failed. WSARecv error: " + IntStr(err)));
            return;
        }
    }
}
void SocketBase::PostSendWork(IocpContext* context)
{
    // Windows-specific implementation for posting a send work
    SOCKET s = stype == SockType::SERVER ? context->acceptSocket : Socket_Listen;
    DWORD bytesSent = 0;
    WSABUF* wsabuf = ~(*context->pack);
    if (!wsabuf || wsabuf->buf == nullptr || wsabuf->len == 0) {
		assetEventBus.Emit(SYSEVENT::SYSERR, "PostWSASend: invalid buffer");
        return;
    }
    int rc = WSASend(s, wsabuf, 1, &bytesSent, 0, &context->ov, NULL);
    if (rc == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
			assetEventBus.Emit(SYSEVENT::SYSERR, ~String("PostWSASend Function failed. WSASend error: " + IntStr(err)));
            return;
        }
    }
}
#endif

bool SocketBase::IsValidIP(const char* host)
{
    // 简单的IPv4格式检查，可根据需要增强为正则表达式
    Vector<String> parts = Split(host, '.');
    if (parts.GetCount() != 4) return false;
    
    for (const String& part : parts) {
        int value = StrInt(part);
        if (value < 0 || value > 255) return false;
    }
    return true;
}
String SocketBase::ResolveHostName(const String& host)
{
    // 使用 U++ 的 HttpRequest 进行DNS解析
    HttpRequest http(host);
    String ip = http.Execute(); // 这里需要根据实际API调整
    // 如果解析失败，返回空字符串
    if (ip.IsEmpty()) return "";
    
    return ip;
}

void SocketBase::epoll_handle(struct epoll_event& ev)
{
#ifdef PLATFORM_WIN32
    if(ev.events & EPOLLCONNET && stype == SockType::SERVER)
        // 服务器socket连接完成事件
		handle_accept(ev);
#else
    if(ev.data.fd == Get() && stype == SockType::SERVER)
        // 服务器socket可读，表示有新连接
        handle_accept(ev);
#endif// PLATFORM_WIN32
    else
        handle_client(ev);
}
bool SocketBase::CreatSocket(SockType type)
{
    assetEventBus.Emit(SYSEVENT::SYSLOG, "Create socket.");
    if(Socket_Listen != INVALID_SOCKET)
    {
		assetEventBus.Emit(SYSEVENT::SYSLOG, "CreatSocket Function failed. Socket has been created");
        return true;
    }

#if defined(PLATFORM_WIN32)
    static bool s_wsa_inited = false;
    if(!s_wsa_inited) {
        WSADATA wsaData;
        int rc = WSAStartup(MAKEWORD(2,2), &wsaData);
        if(rc != 0) {
			assetEventBus.Emit(SYSEVENT::SYSERR, ~String("WSAStartup failed with error: " + IntStr(rc)));
			return false;
        }
        s_wsa_inited = true;
    }
#endif

    // create socket
    if(type == SockType::SERVER || type == SockType::CLIENT) {
#if defined(PLATFORM_WIN32)
        Socket_Listen = ::WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED);
#else
        Socket_Listen = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#endif
        if(Socket_Listen == INVALID_SOCKET) {
#if defined(PLATFORM_WIN32)
            int err = WSAGetLastError();
            assetEventBus.Emit(SYSEVENT::SYSERR, ~String("socket() failed (WSA error): " + IntStr(err)));
            return false;
#else
            int err = errno;
            assetEventBus.Emit(SYSEVENT::SYSERR, ~String("socket() failed error: " + IntStr(err)));
            return false;
#endif
        }
    } else {
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("CreatSocket Function failed. Unsupported socket type"));
        return false;
    }

    // set socket options (SO_REUSEADDR)
    int opt = 1;
#if defined(PLATFORM_WIN32)
    if(setsockopt(Socket_Listen, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
        // best effort cleanup
        ClosedSocket();
        assetEventBus.Emit(SYSEVENT::SYSERR, ~String("setsockopt(SO_REUSEADDR) failed (WSA error): " + IntStr(err)));
        return false;
    }
#else
    if(setsockopt(Socket_Listen, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        int err = errno;
        ClosedSocket();
        assetEventBus.Emit(SYSEVENT::SYSERR, ~String("setsockopt(SO_REUSEADDR) failed error: " + IntStr(err)));
        return false;
    }
#endif

	if(!SetEpoll(Socket_Listen, EPOLLIN, EPOLL_CTL_ADD))
    {
        ClosedSocket();
        return false;
    }
    // 保存类型并返回
    stype = type;
	assetEventBus.Emit(SYSEVENT::SYSLOG, "Create socket....OK");
    return true;
}
void NetLibrary::SocketBase::ClosedSocket()
{
	assetEventBus.Emit(SYSEVENT::SYSLOG, "Close socket.");

    // 1) 先停止事件循环，确保不再消费 I/O 完成
    if (this->GetIsStart())
        this->StopEpoll();

    // 2) 再关闭主侦听 socket
    if (Socket_Listen != INVALID_SOCKET) {
    #if defined(PLATFORM_POSIX)
        SetEpoll(Socket_Listen, 0, EPOLL_CTL_DEL);
        close(Socket_Listen);
        Socket_Listen = INVALID_SOCKET;
    #else
        closesocket(Socket_Listen);
        Socket_Listen = INVALID_SOCKET;
    #endif
    }

    // 3) 客户端 socket应在 StopEpoll 或外层统一遍历关闭
}
;
SOCKET SocketBase::Get()
{
    return Socket_Listen;
}