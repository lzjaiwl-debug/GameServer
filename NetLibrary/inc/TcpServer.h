#ifndef _NetLibrary_TcpServer_h_
#define _NetLibrary_TcpServer_h_

namespace NetLibrary {
class TcpServer : public SocketBase, public SocketFun, public Protocol
{
	typedef TcpServer CLASSNAME;
#ifdef PLATFORM_WIN32
private:
	int accept_concurrency = 10;
	static LPFN_ACCEPTEX lpfnAcceptEx;
	static LPFN_GETACCEPTEXSOCKADDRS lpfnGetAcceptExSockaddrs;
	void InitAcceptEx();
	void PostAcceptWork();
	void PostRecvWork(One<SockPacket> pack);
	void PostSendWork(One<SockPacket> pack);
	void DoAcceptWork(struct epoll_event& ev);
	void DoRecvWork(struct epoll_event& ev);
	void DoSendWork(struct epoll_event& ev);
#endif // PLATFORM_WIN32
protected:
	int pingInterval = 25000; // 默认25秒发送一次ping
	int pingTimeout = 60000;  // 默认60秒超时断开
	NetSSLContext ssl_ctx;
#ifdef PLATFORM_WIN32
	AssetLib::PoolLib<IocpContext, size_t> iocpcontextpool;
#endif // PLATFORM_WIN32
	AssetLib::PoolLib<UserInfo,size_t> users;
	AssetLib::ConcurrentMap<SOCKET, UserInfo*> fdtousers;
	AssetLib::ConcurrentMap<Uuid, UserInfo*, UuidHash> sidtousers;
	VectorMap<SOCKET, Vector<Value>> cachepacks;
	VectorMap<SOCKET, One<SockPacket>> packs;
	AssetLib::TimeEventAsync<void, UserInfo*> tasks;
	Mutex lock;
private:
	void ListenSocket(const char* host, int port);
	void RunServer(const char* host, int port);
	void KickUser(UserInfo* u, bool isDeconnect = false);
	void KickUser(SOCKET s, bool isDeconnect = false);
	void PingPong(UserInfo* u);
	/// <summary>
	/// 处理用户断开连接
	/// </summary>
	/// <param name="u">用户信息</param>
	/// <returns>如果处理了断开连接返回true，否则返回false</returns>
	virtual bool HandleDisconnect(UserInfo* u) {return false;};
protected:
	Event<SOCKET> Whenbusy;
	virtual void EventFun(EventType etype, One<SockPacket>& obj, Value val);
	bool InitSSL(const char* Certificate, const char* PrivateKey);
	virtual void handle_accept(struct epoll_event& ev);
	virtual void handle_client(struct epoll_event& ev);
	virtual void DelUser(Uuid uid);
	virtual void DelUser(UserInfo* u, bool isDeconnect = false);
	virtual void StartServer(const char* host, int port, int unum, size_t memsize);
	void SendEvent(Uuid uid, EMIEVENT ev, AssetLib::BinaryData d, int ackId = -1, bool isBinary = false, bool isReply = false);
	virtual void ShutDown();
public:
	TcpServer() : SocketFun(SockType::SERVER), Protocol(SockType::SERVER) {
		EpollErrorWhenStop << THISFN(ShutDown);
		SocketFun::WhenEvent << THISFN(EventFun);
		Protocol::WhenEvent << THISFN(EventFun);
	}
};
};
#endif
