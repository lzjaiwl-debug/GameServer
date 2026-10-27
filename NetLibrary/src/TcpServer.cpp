#include "../inc/include.h"
#ifdef PLATFORM_WIN32
// define static AcceptEx function pointers
LPFN_ACCEPTEX NetLibrary::TcpServer::lpfnAcceptEx = NULL;
LPFN_GETACCEPTEXSOCKADDRS NetLibrary::TcpServer::lpfnGetAcceptExSockaddrs = NULL;

void NetLibrary::TcpServer::InitAcceptEx()
{
	GUID guidAcceptEx = WSAID_ACCEPTEX;
	GUID guidGetAcceptExSockaddrs = WSAID_GETACCEPTEXSOCKADDRS;
	DWORD bytes;
	if (WSAIoctl(Get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx),
		&lpfnAcceptEx, sizeof(lpfnAcceptEx),
		&bytes, NULL, NULL) == SOCKET_ERROR) {
		ShutDown();
		assetEventBus.Emit(SYSEVENT::SYSERR, "InitAcceptEx Function failed. WSAIoctl AcceptEx");
		return;
	}
	if (WSAIoctl(Get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidGetAcceptExSockaddrs, sizeof(guidGetAcceptExSockaddrs),
		&lpfnGetAcceptExSockaddrs, sizeof(lpfnGetAcceptExSockaddrs),
		&bytes, NULL, NULL) == SOCKET_ERROR) {
		ShutDown();
		assetEventBus.Emit(SYSEVENT::SYSERR, "InitAcceptEx Function failed. WSAIoctl GetAcceptExSockaddrs");
		return;
	}
}
void NetLibrary::TcpServer::PostAcceptWork()
{
	IocpContext* context = iocpcontextpool.TakeOut(true);
	context->pack = pick(CreatSockPacket(NULL, PacketType::RECV, false, false));
	context->pack->ApplyBuf((sizeof(struct sockaddr_in) + 16) * 2);
	context->events = EPOLLCONNET;
	context->acceptSocket = ::WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
	if (context->acceptSocket == INVALID_SOCKET) {
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		assetEventBus.Emit(SYSEVENT::SYSERR, "PostACCEPTEX Function failed. Create accept socket");
		return;
	}
    DWORD bytesReceived;
    BOOL result = lpfnAcceptEx(
        Get(),
        context->acceptSocket,
        *context->pack,
        0,
        sizeof(struct sockaddr_in) + 16,
        sizeof(struct sockaddr_in) + 16,
        &bytesReceived,
        &context->ov);
    if (!result) {
        int err = WSAGetLastError();
        if (err != ERROR_IO_PENDING) {
			iocpcontextpool.ReleaseOb(context);// Return context to pool
            assetEventBus.Emit(SYSEVENT::SYSERR, ~String("PostACCEPTEX Function failed. AcceptEx error: " + IntStr(err)));
            return;
        }
    }
}

void NetLibrary::TcpServer::PostRecvWork(One<SockPacket> pack)
{
	// Post an overlapped WSARecv on socket s
	IocpContext* context = iocpcontextpool.TakeOut(true);
	context->pack = pick(pack);
	context->acceptSocket = context->pack->get_fd();
	context->events = EPOLLIN;
	SocketBase::PostRecvWork(context);
}

void NetLibrary::TcpServer::PostSendWork(One<SockPacket> pack)
{
	// Post an overlapped WSASend on socket s
	IocpContext* context = iocpcontextpool.TakeOut(true);
	context->pack = pick(pack);
	context->acceptSocket = context->pack->get_fd();
	context->events = EPOLLOUT;
	SocketBase::PostSendWork(context);
}
void NetLibrary::TcpServer::DoAcceptWork(struct epoll_event& ev)
{
	IocpContext* context = (IocpContext*)ev.data.ptr;
	SOCKET clientSocket = context->acceptSocket;
	if (clientSocket == INVALID_SOCKET)
	{
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		assetEventBus.Emit(SYSEVENT::SYSERR, "DoAcceptWork Function failed. Invalid accepted socket");
		return;
	}
    // Per Winsock requirement, update the accept context so the accepted socket
    // inherits properties from the listening socket
    SOCKET listenSock = Get();
    if (setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, (char*)&listenSock, sizeof(listenSock)) == SOCKET_ERROR) {
        int err = WSAGetLastError();
		iocpcontextpool.ReleaseOb(context);// Return context to pool
        assetEventBus.Emit(SYSEVENT::SYSERR, ~String("SO_UPDATE_ACCEPT_CONTEXT failed. WSA error: " + IntStr(err)));
        return;
    }
    // register accepted socket with IOCP / our epoll wrapper
	if (!this->set_nonblocking(clientSocket) || !this->SetEpoll(clientSocket, EPOLLIN | EPOLLET | EPOLLRDHUP, EPOLL_CTL_ADD))
	{
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to register accepted socket with epoll");
		return;
	}
	// build user object
	UserInfo* u = users.TakeOut(false);
	if (!u) {
		if(Whenbusy)
			Whenbusy(clientSocket);
		shutdown(clientSocket, SD_BOTH);// Graceful shutdown
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		return;
	}
    u->SetFd(clientSocket);
	u->SetProtocol(ProtocolType::CONNECT);
    if(ssl_ctx != NULL && !u->CreatSSLuser(ssl_ctx))
	{
		KickUser(u, false);
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		return;
	}
	assetEventBus.Emit(SYSEVENT::SYSLOG, ~String("New client connected: " + IntStr((int)clientSocket)));
	// Retrieve local and remote addresses
	sockaddr_in* localAddr = nullptr;
	sockaddr_in* remoteAddr = nullptr;
	int localLen = 0, remoteLen = 0;
    // lpOutputBuffer must be the same buffer passed to AcceptEx
    lpfnGetAcceptExSockaddrs(*context->pack,
        0,
        sizeof(struct sockaddr_in) + 16,
        sizeof(struct sockaddr_in) + 16,
        (SOCKADDR**)&localAddr,
        &localLen,
        (SOCKADDR**)&remoteAddr,
        &remoteLen);
	// Here you can create a UserInfo object for the new connection
	// and add it to your user management structures
	// Clean up
    if (remoteAddr)
        u->SetclientAddr(*remoteAddr);
	fdtousers.InsertOrAssign(clientSocket, u);
	context->acceptSocket = INVALID_SOCKET;
	iocpcontextpool.ReleaseOb(context);// Return context to pool
	// Post a recv to start receiving data from the client
	SubmitWork(u, nullptr, false, false);
}
void NetLibrary::TcpServer::DoRecvWork(epoll_event& ev)
{
    IocpContext* context = (IocpContext*)ev.data.ptr;
    SOCKET s = context->acceptSocket;
    if (s == INVALID_SOCKET) {
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		assetEventBus.Emit(SYSEVENT::SYSERR, "DoRecvWork Function failed. Invalid recv socket");
		return;
    }
	UserInfo* u = nullptr;
	fdtousers.TryGetCopy(s, u);
    if (!u) {
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		assetEventBus.Emit(SYSEVENT::SYSERR, "DoRecvWork Function failed. User not found for socket");
		return;
    }
	context->pack->add_wirte_num((size_t)context->bytesTransferred);
	// Process received data
	u_long bytes_available = 0;
	if (ioctlsocket(s, FIONREAD, &bytes_available) != SOCKET_ERROR)
	{
		if (bytes_available > 0)
		{
			if (bytes_available > context->pack->get_free_space())
				context->pack->addset_free_space(bytes_available);
			// More data available, post another recv
			SocketBase::PostRecvWork(context);
		}
		else
		{
			context->acceptSocket = INVALID_SOCKET;
			One<SockPacket> pack = pick(context->pack);
			iocpcontextpool.ReleaseOb(context);// Return context to pool
			pack->RemoveEmpty();
			SubmitWork(pick(pack));
		}
	}
	else
	{
		context->acceptSocket = INVALID_SOCKET;
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		KickUser(s, false);
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("ioctlsocket FIONREAD failed. WSA error: " + IntStr(WSAGetLastError())));
		return;
	}
}
void NetLibrary::TcpServer::DoSendWork(epoll_event& ev)
{
	IocpContext* context = (IocpContext*)ev.data.ptr;
	SOCKET s = context->acceptSocket;
	if (s == INVALID_SOCKET) {
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		assetEventBus.Emit(SYSEVENT::SYSERR, "DoSendWork Function failed. Invalid send socket");
		return;
	}
	UserInfo* u = nullptr;
	fdtousers.TryGetCopy(s, u);
	if (u == NULL) {
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		assetEventBus.Emit(SYSEVENT::SYSERR, "DoSendWork Function failed. User not found for socket");
		return;
	}
	context->pack->add_read_num((size_t)context->bytesTransferred);
	if (context->pack->get_not_read_num() > 0)
		SocketBase::PostSendWork(context);
	else
	{
		SubmitWork(pick(context->pack));
		//EventFun(EventType::SENDENDEVENT, context->pack, Value());
		context->acceptSocket = INVALID_SOCKET;
		iocpcontextpool.ReleaseOb(context);// Return context to pool
	}
}
#endif // PLATFORM_WIN32
void TcpServer::ListenSocket(const char* host, int port)
{
	assetEventBus.Emit(SYSEVENT::SYSLOG, ~String("Starting TCP server on " + String(host) + ":" + IntStr(port)));
    if (!IsValidIP(host) && this->ResolveHostName(host).IsEmpty())
	{
		ShutDown();
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("CreatSocket Function failed. IP resolution failed for host: " + String(host)));
		return;
	}
    sockaddr_in sin;
    Zero(sin);
    struct in_addr v4;
    int success = inet_pton(AF_INET, host, &v4);
    if (success <= 0) {
#if defined(PLATFORM_WIN32)
		ShutDown();
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("CreatSocket Function failed. Invalid IP address: " + String(host)));
#else
		ShutDown();
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("CreatSocket Function failed. Invalid IP address: " + String(host) + " - " + std::strerror(errno)));
#endif
		return;
    }
    sin.sin_family = AF_INET;
    sin.sin_port = htons(port);
    sin.sin_addr.s_addr = v4.s_addr;
    // bind
#if defined(PLATFORM_WIN32)
    if (bind(Get(), (struct sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
		ShutDown();
        int err = WSAGetLastError();
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("bind() failed. WSA error: " + IntStr(err)));
		return;
    }
    if (listen(Get(), SOMAXCONN) == SOCKET_ERROR) {
		ShutDown();
        int err = WSAGetLastError();
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("listen() failed. WSA error: " + IntStr(err)));
		return;
    }
#else
    if (bind(Get(), (struct sockaddr*)&sin, sizeof(sin)) < 0)
	{
		ShutDown();
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("bind() failed. Error: " + String(std::strerror(errno))));
		return;
	}
    if (listen(Get(), SOMAXCONN) < 0)
	{
		ShutDown();
		assetEventBus.Emit(SYSEVENT::SYSERR, ~String("listen() failed. Error: " + String(std::strerror(errno))));
		return;
	}
#endif
	assetEventBus.Emit(SYSEVENT::SYSLOG, ~String("TCP server is now listening on " + String(host) + ":" + IntStr(port)));
}
void TcpServer::RunServer(const char* host, int port)
{
    if (this->CreatSocket(SockType::SERVER))
    {
#ifdef PLATFORM_WIN32
		// Windows: initialize AcceptEx function pointers
		InitAcceptEx();
        // For Windows IOCP with AcceptEx, ensure socket is bound and listening
        ListenSocket(host, port);
        // Pre-post multiple AcceptEx requests to handle concurrent connects
        for (int i = 0; i < accept_concurrency; ++i)
            PostAcceptWork();
        // Start the loop
        RunLoop();
#else
        // POSIX: bind/listen then associate with epoll and start loop
        ListenSocket(host, port);
        RunLoop();
#endif
    }
}
void TcpServer::StartServer(const char* host, int port, int unum, size_t memsize)
{
	if(!tasks.IsRun())
		tasks.TimeRun(1000);//设置1秒一次心跳
#ifdef PLATFORM_WIN32
	if(iocpcontextpool.GetAllNum() == 0)
		iocpcontextpool.CreatNewList(unum * 2);
#endif // PLATFORM_WIN32
	if (users.GetAllNum() == 0)
		users.CreatNewList(unum);
	SetMemorySize(memsize);
	RunServer(host, port);
}
void TcpServer::KickUser(UserInfo* u, bool isDeconnect)
{
	Mutex::Lock __(lock);
	assetEventBus.Emit(SYSEVENT::SYSLOG, ~String("Kicking user: " + IntStr((int)u->GetFd())));
	assetEventBus.Emit(SYSEVENT::DISCONNECT, ~u->Getsid().ToStringWithDashes());
	if (u->GetProtocol() == ProtocolType::SOCKETIO && !isDeconnect)
		SubmitWork(u, ~sio_message(frame_type::frame_message, state_type::type_disconnect));
	else if (u->GetProtocol() != ProtocolType::CONNECT && !isDeconnect)
		SubmitWork(u, "kick");
#ifdef PLATFORM_POSIX
	SetEpoll(u->GetFd(), 0, EPOLL_CTL_DEL);
#endif // PLATFORM_POSIX
	tasks.RemoveTaskIf([&u](const std::tuple<UserInfo*>& payload) {
		return std::get<0>(payload) == u;
	});
	fdtousers.Erase(u->GetFd());
	sidtousers.Erase(u->Getsid());
	users.ReleaseOb(u);
}
void NetLibrary::TcpServer::KickUser(SOCKET s, bool isDeconnect)
{
	Mutex::Lock __(lock);
	UserInfo* u = nullptr;
	fdtousers.TryGetCopy(s, u);
	if (u == NULL)
	{
#ifdef PLATFORM_POSIX
		shutdown(s, SHUT_RDWR);
		close(s);
#else
		shutdown(s, SD_BOTH);
		closesocket(s);
#endif
		return;
	}
	KickUser(u, isDeconnect);
}
void TcpServer::PingPong(UserInfo* u)
{
	Mutex::Lock __(lock);
	::Time t = ::GetSysTime();
	if (u->GetPingPong() == 0)
	{
		if (u->GetProtocol() == ProtocolType::SOCKETIO)
			SubmitWork(u, ~sio_message(frame_type::frame_ping), false);
		else		
			SubmitWork(u, OPCODE::PING);
		u->SetPingPong(::GetUTCSeconds(t));
		return;
	}
	if (u->GetPingPong() - ::GetUTCSeconds(t) > pingTimeout)
		KickUser(u);
}
bool TcpServer::InitSSL(const char* Certificate, const char* PrivateKey)
{
	if (ssl_ctx != NULL)
		return true;
	if(ssl_ctx.LoadCertificate(Certificate, PrivateKey) && ssl_ctx.InitSSLContext())
		return true;
	ssl_ctx.Clear();
	return false;
}

void TcpServer::EventFun(EventType etype, One<SockPacket>& obj, Value val)
{
	Mutex::Lock __(lock);
	One<SockPacket> pack = pick(obj);
	UserInfo* user = nullptr;
	fdtousers.TryGetCopy(pack->get_fd(), user);
	if (user == NULL)
		return;
	switch (etype) {
	case EventType::SENDCACHENULL: // 写缓冲满了，将客户端的epoll修改为包含写完成事件
#ifdef PLATFORM_POSIX
		{
			SubmitWork(pick(pack), false, false);
			SetEpoll(user->GetFd(), EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT, EPOLL_CTL_MOD);
		}
#endif // PLATFORM_POSIX
		break;
	case EventType::SESSIONEVENT:
		{
			user->Setsid(val);
		}
		break;
	case EventType::CLOSEDEVENT:
		{
			KickUser(user);
		}
		break;
	case EventType::ERROREVENT:
		{
		    SubmitWork(user, ~pack->tostring());
			KickUser(user);
		}
		break;
	case EventType::SOCKETIOOPEN:
		{
			::Uuid sid = Uuid::Create();
			user->Setsid(sid);
			sidtousers.InsertOrAssign(sid, user);
			String json = sio_message(frame_type::frame_open)
				.SetVal("sid", sid.ToStringWithDashes())
				.SetVal("upgrades", ValueArray({ "websocket" }))
				.SetVal("pingInterval", pingInterval)
				.SetVal("pingTimeout", pingTimeout);
			if (val.Is<bool>() && val.Get<bool>())
			{
				SubmitWork(user,(const char*)*pack);
				SubmitWork(user, ~json);
			}
			else
				SubmitWork(user, ~buildHandshake(json, "", "", ProtocolType::SOCKETIO));
		}
		break;
	case EventType::SOCKETIOPOLL:
		{
			if(val.Is<Uuid>())
			{
				::Uuid sid = val.Get<Uuid>();
				if (sidtousers.Contains(sid))
					SubmitWork(user, (const char*)*pack);
				else
				{
					SubmitWork(user, ~sio_message(frame_type::frame_message, state_type::type_error)
						.SetVal("req", "polling")
						.SetVal("code", 1)
						.SetVal("message", "Session ID unknown"));
				}
			}
		}
		break;
	case EventType::SOCKETHANDSHAKE:
		{
		SubmitWork(user, (const char*)*pack);
		Clientinfo info(~user->Getsid().ToStringWithDashes(), user->GetclientIP(), false);
		AssetLib::BinaryData bin;
		AssetLib::SerializerFactory::Serialize(info, bin);
		assetEventBus.Emit(SYSEVENT::CONNECT, bin);//握手成功
		if(val.Is<bool>() && val.Get<bool>())
			user->SetProtocol(ProtocolType::SOCKET);
			
		else
			user->SetProtocol(ProtocolType::WEBSOCKET);
		}
		break;
	case EventType::SOCKETIOHANDSHAKE:
		{
		    SubmitWork(user, (const char*)*pack);
			user->SetProtocol(ProtocolType::SOCKETIO);
		}
		break;
	case EventType::SIODATAEVENT:
	{
		sio_packet spack(pick(pack));
		if (spack.get_frame() == frame_type::frame_ping && spack.get_data() == "probe")
		{
			SubmitWork(user, ~sio_message(frame_type::frame_pong).AddVal("probe"));
			return;
		}
		if (spack.get_frame() == frame_type::frame_upgrade) {
			tasks.CreateTask({ this, &TcpServer::PingPong }, pingInterval / 1000, user);
			AssetLib::BinaryData bin;
			Clientinfo info(~user->Getsid().ToStringWithDashes(), user->GetclientIP(), true);
			AssetLib::SerializerFactory::Serialize(info, bin);
			assetEventBus.Emit(SYSEVENT::CONNECT, bin);//升级成功握手成功
			return;
		}
		if (spack.get_frame() == frame_type::frame_message && spack.get_state() == state_type::type_connect) {
			SubmitWork(user, ~sio_message(frame_type::frame_open, state_type::type_connect)
				.SetVal("sid", user->Getsid().ToStringWithDashes()));
			tasks.CreateTask({ this, &TcpServer::PingPong }, pingInterval / 1000, user);
			AssetLib::BinaryData bin;
			Clientinfo info(~user->Getsid().ToStringWithDashes(), user->GetclientIP(), true);
			AssetLib::SerializerFactory::Serialize(info, bin);
			assetEventBus.Emit(SYSEVENT::CONNECT, bin);//非轮询情况下握手成功
			return;
		}
		if (spack.get_frame() == frame_type::frame_message && (spack.get_state() == state_type::type_event || spack.get_state() == state_type::type_ack)) {
			Value data = spack.get_data();
			if (data.IsError()) {
				SubmitWork(user,
					~sio_message(SYSEVENT::DATAERR, frame_type::frame_message, state_type::type_error)
					.SetVal("req", "event")
					.SetVal("code", 4)
					.SetVal("message", "Forbidden"));
				assetEventBus.Emit(SYSEVENT::DATAERR, "Data is empty or not a valid protocol type.");
				return;
			}
			SockData sd(false, spack.get_ackid(), ~user->Getsid().ToStringWithDashes(), ~AsJSON(data[1]));
			AssetLib::BinaryData bin;
			AssetLib::SerializerFactory::Serialize(sd, bin);
			assetEventBus.Emit(~~data[0], bin);
			return;
		}
		if (spack.get_frame() == frame_type::frame_message &&
			(spack.get_state() == state_type::type_binary_event || spack.get_state() == state_type::type_binary_ack))
		{
			Value data = spack.get_data();
			for (int i = 0; i < data[1].GetCount(); i++)
			{
				if (data[1][i]["_placeholder"])
				{
					Value item;
					item.GetAdd("event") = data[0];
					item.GetAdd("ackid") = spack.get_ackid();
					cachepacks.Get(user->GetFd()).Add(item);
				}
			}
			return;
		}
		if (spack.get_frame() == frame_type::frame_unknown && cachepacks.Find(user->GetFd()) != -1 && cachepacks.Get(user->GetFd()).GetCount() > 0) {
			Value item = cachepacks.Get(user->GetFd()).Pop();
			SockData sd(true, item["ackid"], ~user->Getsid().ToStringWithDashes(), ~~spack.get_data());
			AssetLib::BinaryData bin;
			AssetLib::SerializerFactory::Serialize(sd, bin);
			assetEventBus.Emit(~~item["event"], bin);
			if (cachepacks.Get(user->GetFd()).GetCount() == 0)
				cachepacks.RemoveKey(user->GetFd());
			return;
		}
		if (spack.get_frame() == frame_type::frame_pong)
		{
			user->SetPingPong(0);
			return;
		}
		if (spack.get_frame() == frame_type::frame_error)
		{
			this->KickUser(user, true);
			return;
		}
	}
	    break;
	case EventType::PRODATAEVENT:
	{
		if (user->GetProtocol() == ProtocolType::SOCKETIO) {
			ParseHttpPro(pick(pack), ProtocolType::SOCKETIO);
		}
		else {
			if (pack->IsBinary())
			{
				NoSioData data;
				AssetLib::BinaryData tmpd;
				pack->refData(tmpd);
				AssetLib::SerializerFactory::Deserialize(data, tmpd);
				SockData sd(true, -1, ~user->Getsid().ToStringWithDashes(), data._data);
				AssetLib::BinaryData bin;
				AssetLib::SerializerFactory::Serialize(sd, bin);
				assetEventBus.Emit(data._event, bin);
			}
			else
			{
				Value data = ParseJSON(pack->tostring());
				SockData sd(true, -1, ~user->Getsid().ToStringWithDashes(), ~~data[1]);
				AssetLib::BinaryData bin;
				AssetLib::SerializerFactory::Serialize(sd, bin);
				assetEventBus.Emit(~~data[0], bin);
			}
		}
	}
		break;
	case EventType::DATAEVENT:
	{
		if(val.Is<bool>() && !val.Get<bool>())
		{
#ifdef _WIN32
			PostRecvWork(pick(pack));
#else
			SubmitWork(pick(pack));
#endif
		}
		else
		{
#ifdef _WIN32
			SubmitWork(user, nullptr, false, false);
#endif
			ParseHttpPro(pick(pack));
		}
	}
		break;
	case EventType::PINGPONG:
		{
			user->SetPingPong(0);
		}
		break;
	case EventType::SENDENDEVENT:
	{
			if (val.Is<bool>() && !val.Get<bool>())
#ifdef _WIN32
				PostSendWork(pick(pack));
#else
				SubmitWork(pick(pack));
#endif
#ifdef PLATFORM_POSIX
			else
				SetEpoll(user->GetFd(), EPOLLIN | EPOLLET | EPOLLRDHUP, EPOLL_CTL_MOD);
#endif // PLATFORM_POSIX	
	}
		break;
	default:
		break;
	}
}
void TcpServer::handle_accept(struct epoll_event& ev)
{
#ifdef PLATFORM_WIN32
    // If this event carries an overlapped pointer it likely represents an AcceptEx completion
    if(ev.data.ptr) {
		PostAcceptWork();
		DoAcceptWork(ev);
        return;
    }
#else
	socklen_t addrLen = sizeof(sockaddr_in);
	sockaddr_in addr;
	// 9. 接受新连接
	int clientFd =
		accept4(Get(), (struct sockaddr*)&addr, &addrLen, SOCK_NONBLOCK);
	if (clientFd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			assetEventBus.Emit(SYSEVENT::SYSERR, "accept() returned EAGAIN/EWOULDBLOCK, no more connections to accept");
			return;
		}
		assetEventBus.Emit(SYSEVENT::SYSERR, "accept() failed. Error: " + String(std::strerror(errno)));
		return;
	}
	UserInfo* u = users.TakeOut(false);
	if (!u)//超过最大并发数量 直接不允许接收新连接
	{
		shutdown(clientFd, SHUT_RDWR);//关闭连接
		close(clientFd);
		return;
	}
	// 10. 设置客户端socket为非阻塞模式
	try {
		u->GetclientAddr() = addr;
		u->SetFd(clientFd);
		u->SetProtocol(ProtocolType::CONNECT);
		if (ssl_ctx != NULL && !u->CreatSSLuser(ssl_ctx))
		{
			KickUser(u);
			assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to create SSL user for new connection");
			return;
		}
		if(set_nonblocking(clientFd) && SetEpoll(clientFd, EPOLLIN | EPOLLET | EPOLLRDHUP, EPOLL_CTL_ADD))
			fdtousers.InsertOrAssign(clientFd, u);
		else
		{
			KickUser(u);
			assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to set non-blocking mode or add client socket to epoll");
			return;
		}
	}
	assetEventBus.Emit(SYSEVENT::SYSLOG, ~Format("New client connected: , total clients: ", clientFd, users.GetIdleNum()));
#endif
}
void TcpServer::handle_client(struct epoll_event& ev)
{
	if (!fdtousers.Contains(ev.data.fd))
	{
		if (ev.data.fd > 0)
		{
			KickUser(ev.data.fd);
			assetEventBus.Emit(SYSEVENT::SYSLOG, ~Format("Unknown user disconnected: %d", (int)ev.data.fd));
		}
		return;
	}
	UserInfo* u = nullptr;
	fdtousers.TryGetCopy(ev.data.fd, u);
	if (!u)
		return;
    // Handle SSL handshake:
    // - On POSIX (epoll) we use synchronous/non-blocking SSL_accept driven from events
    // - On Windows (IOCP) handshake is driven asynchronously when WSARecv completions
    //   are processed in DoRecvWork which feeds data into the SSL rbio and calls
    //   HandleSSLHandshake there. Avoid calling SSL_accept directly on Windows here.
//#if defined(PLATFORM_POSIX)
//    if (u->GetProtocol() == ProtocolType::CONNECT && u->GetSSL() && !HandleSSLHandshake(ev))
//        return;
//#endif
	// 处理错误和挂断事件（优先级最高）
	if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
		if (!HandleDisconnect(u))
		{
			assetEventBus.Emit(SYSEVENT::DISCONNECT, ~u->Getsid().ToStringWithDashes());
			KickUser(u, true);
		}
	}
	if (ev.events & EPOLLOUT) {
#ifdef PLATFORM_WIN32
		DoSendWork(ev);
#else
		SubmitWork(u, nullptr); // 触发发送工作任务执行
#endif // PLATFORM_WIN32
	}
	if (ev.events & EPOLLIN) {
#ifdef PLATFORM_WIN32
		DoRecvWork(ev);
#else
		SubmitWork(u, nullptr, false, false);
#endif // PLATFORM_WIN32
	}
}
void TcpServer::DelUser(Uuid uid)
{
	UserInfo* u = nullptr;
	sidtousers.TryGetCopy(uid, u);
	if (!u)
		return;
	assetEventBus.Emit(SYSEVENT::DISCONNECT, ~u->Getsid().ToStringWithDashes());
	this->KickUser(u, true);
}
void NetLibrary::TcpServer::DelUser(UserInfo* u, bool isDeconnect)
{
	assetEventBus.Emit(SYSEVENT::DISCONNECT, ~u->Getsid().ToStringWithDashes());
	KickUser(u, isDeconnect);
}
void TcpServer::SendEvent(Uuid uid, EMIEVENT ev, AssetLib::BinaryData d, int ackId, bool isBinary, bool isReply)
{
	UserInfo* u = nullptr;
	sidtousers.TryGetCopy(uid, u);
	if (!u)
	{
		assetEventBus.Emit(SYSEVENT::SYSERR, ~Format("SendEvent Function failed. User not found for uid: %s", ~uid.ToStringWithDashes()));
		return;
	}
	if (u->GetProtocol() == ProtocolType::SOCKETIO) {
		if (isBinary) {
			SubmitWork(u, ~sio_message(ev, frame_type::frame_message, isReply ? state_type::type_binary_ack : state_type::type_binary_event, ackId), true);
			SubmitWork(u, d, true);
		}
		else
			SubmitWork(u, ~sio_message(ev, frame_type::frame_message, isReply ? state_type::type_ack : state_type::type_event, ackId).AddVal(String(d)));
	}
	else
	{
		AssetLib::BinaryData bin;
		if (isBinary)
		{
			NoSioData data(ev, pick(d));
			AssetLib::SerializerFactory::Serialize(data, bin);
		}
		else
		{
			Value val;
			val.Add(ev);
			val.Add(String(d));
			bin = ~AsJSON(val);
		}
		SubmitWork(u, pick(bin), false);
	}

}
void NetLibrary::TcpServer::ShutDown()
{
	ClosedSocket();
	Vector<UserInfo*> users_snapshot;
	fdtousers.ForEach([&users_snapshot](const SOCKET&, UserInfo* u) {
		if (u)
			users_snapshot.Add(u);
	});
	for (auto* u : users_snapshot)
		KickUser(u, true);
	tasks.Stop();
#ifdef PLATFORM_WIN32
	iocpcontextpool.Clear();
#endif
	ShutDownFn();
	cachepacks.Clear();
	sidtousers.Clear();
	fdtousers.Clear();
	users.Clear();
	ssl_ctx.Clear();
#if defined(PLATFORM_WIN32)
	WSACleanup();
#endif
}
