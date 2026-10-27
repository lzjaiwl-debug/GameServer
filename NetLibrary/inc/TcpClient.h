#pragma once
namespace NetLibrary {
	class TcpClient : public SocketBase, public SocketFun, public Protocol {
		typedef TcpClient CLASSNAME;
#ifdef PLATFORM_WIN32
	private:
		static LPFN_CONNECTEX lpfnConnectEx;
		void InitConnectEx();
		void PostConnectWork(SOCKET s, const struct sockaddr* addr, int addrlen);
		void PostRecvWork(One<SockPacket> pack);
		void PostSendWork(One<SockPacket> pack);
		void DoConnectWork(struct epoll_event& ev);
		void DoRecvWork(struct epoll_event& ev);
		void DoSendWork(struct epoll_event& ev);
#endif // PLATFORM_WIN32
	protected:
		Mutex lock;
		virtual void handle_client(struct epoll_event& ev);
	private:
		int pingInterval = 0;
		int pingTimeout = 0;
		ProtocolType ptype = ProtocolType::SOCKET;
		UserInfo clientinfo;
		AssetLib::TimeEventAsync<void> tasks;
		Vector<Value> binary_list;
#ifdef PLATFORM_WIN32
		AssetLib::PoolLib<IocpContext, size_t> iocpcontextpool;
#endif // PLATFORM_WIN32
	private:
		void PingPong();
		virtual void EventFun(EventType etype, One<SockPacket>& obj, Value val);
		void HandleClose();
		void HandResponse(One<SockPacket> obj, Value pro);
	public:
		TcpClient() : SocketFun(SockType::CLIENT), Protocol(SockType::CLIENT){
		}
		void ConnectServer(const char* host, int port, size_t memsize);
		void SendEvent(EMIEVENT ev, AssetLib::BinaryData d, bool isBinary = false);
	};










};