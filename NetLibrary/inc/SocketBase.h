#ifndef _NetLibrary_SocketBase_h_
#define _NetLibrary_SocketBase_h_
namespace NetLibrary {
	class SocketBase : public EpollServer {
		typedef SocketBase CLASSNAME;
#ifdef PLATFORM_WIN32
	protected:
		void PostRecvWork(IocpContext* context);
		void PostSendWork(IocpContext* context);
#endif
	protected:
		// 判断字符串是否为有效的IPv4地址
		bool IsValidIP(const char* host);
		// 解析域名获取IP地
		String ResolveHostName(const String& host);
	private:
		SOCKET Socket_Listen = INVALID_SOCKET;
		SockType stype;
	private:
		void epoll_handle(struct epoll_event& ev);
	protected:
		SOCKET Get();
		void SetSocketNull(){ Socket_Listen = INVALID_SOCKET; }
		SockType GetSockType() { return stype; };
		bool CreatSocket(SockType type);
		virtual void handle_accept(struct epoll_event& ev){};
		virtual void handle_client(struct epoll_event& ev){};
	public:
		SocketBase(){
			Init();
			handle_event << THISFN(epoll_handle);
		}
		void ClosedSocket();
	};
}; // namespace
#endif
