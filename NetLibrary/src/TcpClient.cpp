#include "../inc/include.h"
#ifdef PLATFORM_WIN32
// define static ConnectEx function pointer
LPFN_CONNECTEX NetLibrary::TcpClient::lpfnConnectEx = NULL;
void NetLibrary::TcpClient::InitConnectEx()
{
	GUID guidConnectEx = WSAID_CONNECTEX;
	DWORD bytes;
	if (WSAIoctl(Get(), SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidConnectEx, sizeof(guidConnectEx),
		&lpfnConnectEx, sizeof(lpfnConnectEx),
		&bytes, NULL, NULL) == SOCKET_ERROR) {
		throw NetException(ExcType::SOCKETERR, WSAGetLastError(), (int64)this,
			"InitConnectEx Function failed. WSAIoctl ConnectEx");
	}
}

void NetLibrary::TcpClient::PostConnectWork(SOCKET s, const sockaddr* addr, int addrlen)
{
	IocpContext* context = iocpcontextpool.TakeOut(true);
	context->events = EPOLLCONNET;
	// 修复C6387警告：ConnectEx的第6个参数（DWORD*）不能为NULL
	DWORD bytesSent = 0;
	BOOL result = lpfnConnectEx(
		s,
		addr,
		addrlen,
		NULL,
		0,
		&bytesSent, // 必须传递非NULL指针
		&context->ov);
	if (!result) {
		int err = WSAGetLastError();
		if (err != ERROR_IO_PENDING) {
			iocpcontextpool.ReleaseOb(context);// Return context to pool
			throw NetException(ExcType::SOCKETERR, err, (int64)this,
				"PostConnectEx Function failed. ConnectEx");
		}
	}
}

void NetLibrary::TcpClient::PostRecvWork(One<SockPacket> pack)
{
	IocpContext* context = new IocpContext();
	context->pack = pick(pack);
	context->events = EPOLLIN;
	SocketBase::PostRecvWork(context);
}
void NetLibrary::TcpClient::PostSendWork(One<SockPacket> pack)
{
	IocpContext* context = new IocpContext();
	context->pack = pick(pack);
	context->events = EPOLLOUT;
	SocketBase::PostSendWork(context);
}
void NetLibrary::TcpClient::DoConnectWork(epoll_event& ev)
{
	IocpContext* context = (IocpContext*)ev.data.ptr;
	// Complete the connection
	BOOL result = setsockopt(Get(), SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0);
	if (result == SOCKET_ERROR) {
		int err = WSAGetLastError();
		iocpcontextpool.RemoveOb(context);// Remove from pool without returning
		HandleClose();
		throw NetException(ExcType::SOCKETERR, err, (int64)this,
			"DoConnectWork Function failed. SO_UPDATE_CONNECT_CONTEXT");
	}
	iocpcontextpool.RemoveOb(context);// Remove from pool without returning
	// Connection established, can now use the socket
	clientinfo.SetProtocol(ProtocolType::CONNECT);
	SubmitWork(&clientinfo,nullptr ,false, false);
}
void NetLibrary::TcpClient::DoRecvWork(epoll_event& ev)
{
	IocpContext* context = (IocpContext*)ev.data.ptr;
	context->pack->add_wirte_num((size_t)context->bytesTransferred);
	// Process received data
	u_long bytes_available = 0;
	if (ioctlsocket(Get(), FIONREAD, &bytes_available) != SOCKET_ERROR)
	{
		if (bytes_available > 0)
		{
			if (bytes_available > context->pack->get_free_space())
				context->pack->addset_free_space(bytes_available);
			// More data available, post another recv
			SocketBase::PostRecvWork(context);
			return;
		}
		else
		{
			context->pack->RemoveEmpty();
			SubmitWork(pick(context->pack));
		}
	}
	else
	{
		iocpcontextpool.ReleaseOb(context);// Return context to pool
		throw NetException(ExcType::SOCKETERR, WSAGetLastError(), (int64)this, "ioctlsocket FIONREAD failed");
	}
	iocpcontextpool.ReleaseOb(context);// Return context to pool
}
void NetLibrary::TcpClient::DoSendWork(struct epoll_event& ev)
{
	IocpContext* context = (IocpContext*)ev.data.ptr;
	context->pack->add_read_num((size_t)context->bytesTransferred);
	if (context->pack->get_not_read_num() > 0)
		SubmitWork(pick(context->pack));
	iocpcontextpool.ReleaseOb(context);// Return context to pool
}
#endif 
void NetLibrary::TcpClient::PingPong()
{
	Mutex::Lock __(lock);
	::Time t = ::GetSysTime();
	if (clientinfo.GetPingPong() == 0)
		clientinfo.SetPingPong(::GetUTCSeconds(t));
	if (clientinfo.GetPingPong() - ::GetUTCSeconds(t) > pingTimeout)
		HandleClose();
}
// PLATFORM_WIN32
void NetLibrary::TcpClient::EventFun(EventType etype, One<SockPacket>& obj, Value val)
{
	One<SockPacket> pack = pick(obj);
	switch (etype)
	{
	case EventType::SOCKETIOHANDSHAKE:
	{
		if (val.Is<bool>() && val.Get<bool>())
			SubmitWork(&clientinfo, (const char*)*pack);
		clientinfo.SetProtocol(ProtocolType::SOCKETIO);
		clientinfo.Setsid(ScanUuid(~val));
		if (pingInterval > 0 && pingTimeout > 0)
			tasks.CreateTask({ this,&TcpClient::PingPong }, pingInterval);
	}
		break;
	case EventType::SOCKETIOPOLL:
	{
		SubmitWork(&clientinfo, (const char*)*pack);
		if (val.Is<sio_message>() && val.Get<sio_message>()._type == frame_type::frame_open)
		{
			const sio_message& sio_msg = val.Get<sio_message>();
			pingInterval = sio_msg["pingInterval"].Is<double>() ? (int)sio_msg["pingInterval"].Get<double>() : ::StrInt(~sio_msg["pingInterval"]);
			pingTimeout = sio_msg["pingTimeout"].Is<double>() ? (int)sio_msg["pingTimeout"].Get<double>() : ::StrInt(~sio_msg["pingTimeout"]);
		}
	}
		break;
	case EventType::PRODATAEVENT:
	{
		if (ptype == ProtocolType::SOCKETIO)
		{
			sio_packet spack = pick(pack);
			if (spack.get_frame() == frame_type::frame_ping)
			{
				if (spack.get_data().ToString() == "probe")
				{
					SubmitWork(&clientinfo, ~sio_message(frame_type::frame_pong).AddVal("probe"));
					SubmitWork(&clientinfo, ~sio_message(frame_type::frame_upgrade));
					return;
				}
				clientinfo.SetPingPong(0);
				SubmitWork(&clientinfo, ~sio_message(frame_type::frame_pong));
			}
			if (spack.get_frame() == frame_type::frame_message && (spack.get_state() == state_type::type_event || spack.get_state() == state_type::type_ack))
			{
				Value data = spack.get_data();
				if (data.IsError() || data.IsNull())
					throw NetException(ExcType::PROERR, -1, int64(this), "Data is empty or not a valid protocol type.");
				SockData sd(true, spack.get_ackid(), ~clientinfo.Getsid().ToStringWithDashes(), ~AsJSON(data[1]));
				AssetLib::BinaryData bin;
				AssetLib::SerializerFactory::Serialize(sd, bin);
				assetEventBus.Emit(~~data[0], bin);
			}
			if (spack.get_frame() == frame_type::frame_message && (spack.get_state() == state_type::type_binary_event || spack.get_state() == state_type::type_binary_ack))
			{
				Value data = spack.get_data();
				for (auto& a : data[1])
				{
					Value item;
					item.GetAdd("event") = data[0];
					item.GetAdd("ackid") = spack.get_ackid();
					binary_list.Add(item);
				}
			}
			if (spack.get_frame() == frame_type::frame_unknown && binary_list.GetCount() > 0)
			{
				Value item = binary_list.Pop();
				SockData sd(true, item["ackid"], ~clientinfo.Getsid().ToStringWithDashes(), ~~spack.get_data());
				AssetLib::BinaryData bin;
				AssetLib::SerializerFactory::Serialize(sd, bin);
				assetEventBus.Emit(~~item["event"], bin);
			}
		}
		else
		{
			if (pack->IsBinary())
			{
				NoSioData data;
				AssetLib::BinaryData tmpd;
				pack->refData(tmpd);
				AssetLib::SerializerFactory::Deserialize(data, tmpd);//反序列化
				SockData sd(true, -1, ~clientinfo.Getsid().ToStringWithDashes(), data._data);
				AssetLib::BinaryData bin;
				AssetLib::SerializerFactory::Serialize(sd, bin);
				assetEventBus.Emit(data._event, bin);
			}
			else
			{
				Value data = ParseJSON(pack->tostring());
				SockData sd(true, -1, ~clientinfo.Getsid().ToStringWithDashes(), ~~data[1]);
				AssetLib::BinaryData bin;
				AssetLib::SerializerFactory::Serialize(sd, bin);
				assetEventBus.Emit(~~data[0], bin);
			}
		}
	}
	break;
	case NetLibrary::EventType::DATAEVENT:
	{
		if (val.Is<bool>() && !val.Get<bool>())
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
			SubmitWork(&clientinfo, nullptr, false, false);
#endif
			ParseHttpPro(pick(pack), clientinfo.GetProtocol());
		}
	}
		break;
	case NetLibrary::EventType::HTTPRESPONSE:
		HandResponse(pick(pack), val);
		break;
	case NetLibrary::EventType::SENDENDEVENT:
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
	case NetLibrary::EventType::SESSIONEVENT:
		break;
	case NetLibrary::EventType::CLOSEDEVENT:
		break;
	case NetLibrary::EventType::ERROREVENT:
		break;
	case NetLibrary::EventType::SENDCACHENULL:
		break;
	default:
		break;
	}
}
void NetLibrary::TcpClient::HandleClose()
{
	Mutex::Lock __(lock);
	ClosedSocket();
}
void NetLibrary::TcpClient::HandResponse(One<SockPacket> obj, Value pro)
{
	assetEventBus.Emit("Response", ~obj->tostring());
}
void NetLibrary::TcpClient::handle_client(struct epoll_event& ev)
{
#ifdef PLATFORM_WIN32
	if (ev.events & EPOLLCONNET) {
		DoConnectWork(ev);
	}
	if (ev.events & EPOLLIN)
	{
		DoRecvWork(ev);
	}
	if (ev.events & EPOLLOUT)
	{

	}
#else
	SocketBase::handle_client(ev);
#endif
}
void NetLibrary::TcpClient::ConnectServer(const char* host, int port, size_t memsize)
{
	if (!tasks.IsRun())
		tasks.TimeRun(1000);//设置1秒一次心跳
#ifdef PLATFORM_WIN32
	if (iocpcontextpool.GetAllNum() == 0)
		iocpcontextpool.CreatNewList(10);
#endif // PLATFORM_WIN32
	SetMemorySize(memsize);
	String ip;
	if (IsValidIP(host))
		ip = host;
	else
		ip = this->ResolveHostName(host);
	if (ip.IsEmpty())
		throw new NetException(ExcType::SOCKETERR, -1, (int64)this,
			"ConnectServer Function failed. IP resolution failed");
	sockaddr_in sin;
	Zero(sin);
	struct in_addr v4;
	int success = inet_pton(AF_INET, ip, &v4);
	sin.sin_family = AF_INET;
	sin.sin_port = htons(port);
	sin.sin_addr.s_addr = success;
	if (this->CreatSocket(SockType::CLIENT))
	{
#ifdef PLATFORM_WIN32
		InitConnectEx();
		if (bind(Get(), (SOCKADDR*)&sin, sizeof(sin)) == SOCKET_ERROR)
		{
			int err = WSAGetLastError();
			ClosedSocket();
			throw NetException(ExcType::SOCKETERR, err, (int64)this, "bind() failed (WSA error)");
		}
		PostConnectWork(Get(), (SOCKADDR*)&sin, sizeof(sin));
#else
		if (connect(Get(), (struct sockaddr*)&sin, sizeof(sin)) < 0)
		{
			ClosedSocket();
			throw NetException(ExcType::SOCKETERR, errno, (int64)this, std::strerror(errno));
		}
		RunLoop();
#endif
	}
}

void NetLibrary::TcpClient::SendEvent(EMIEVENT ev, AssetLib::BinaryData d, bool isBinary)
{
	One<SockPacket> pack;
	this->CreatSockPacket(Get(), PacketType::SEND, false, isBinary, clientinfo.GetSSL_Ptr());
	pack->SetHandshake(true);
	pack->SetBinary(isBinary);
	if (clientinfo.GetProtocol() == ProtocolType::SOCKETIO)
	{
		if (isBinary) {
			SubmitWork(&clientinfo, ~sio_message(ev, frame_type::frame_message, state_type::type_binary_event), true);
			SubmitWork(&clientinfo, d, true);
		}
		else
			SubmitWork(&clientinfo, ~sio_message(ev, frame_type::frame_message, state_type::type_event).AddVal(String(d)));
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
		SubmitWork(&clientinfo, pick(bin), false);
	}
}
