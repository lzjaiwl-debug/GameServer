#include "../inc/include.h"

bool NetLibrary::HttpServer::Gzip(AssetLib::BinaryData& zipbuf)
{
	AssetLib::BinaryData tmp;//临时数据对象
	tmp = pick(zipbuf);//将原对象所有权给临时对象
	String gzip = GZCompress(tmp.data, tmp.size);
	if (IsVoid(gzip))
		return false;
	zipbuf = pick(AssetLib::BinaryData(~gzip, gzip.GetLength()));
	return true;
}
bool NetLibrary::HttpServer::CheckCookieExpiration(const char* dom, const char* sid)
{
	if (!dom || !sid) return true;
	Cookie* co = cookie.FindSeeion(dom, sid);
	if(!co)
		return true;
	// Expires 存储的是绝对过期时间戳（UTC秒），0 表示会话cookie不检查过期
	String expires_str = (*co)["expires"];
	int64 expires = expires_str.IsEmpty() ? 0 : ScanInt64(expires_str);
	if(expires > 0)
	{
		int64 now = ::GetUTCSeconds(::GetSysTime());
		if(now >= expires)
		{
			// cookie已过期，从内存和磁盘中删除
			cookie.DelSeesion(dom, sid);
			String path = Httplib::MakeCookiePathStd(cachefile, dom, sid);
			if(FileExists(path))
				FileDelete(path);
			return true;
		}
	}
	return false;
}
bool NetLibrary::HttpServer::RWCookie(const char* dom, const char* sid, bool isWrite)
{
	if (!dom || !sid) return false;
	if(!isWrite)
	{
		cookie.DeserializeFromDisk(dom, sid, cachefile);
		Cookie* co = cookie.FindSeeion(dom, sid);
		if(!co)
			return false;
		return !CheckCookieExpiration(dom, sid);//先检查是否过期，过期了就删除
	}
	else
	{
		Cookie* co = cookie.FindSeeion(dom, sid);
		if(!co)
			return false;
		cookie.SerializeToDisk(dom, sid, cachefile);
		cookie.DelSeesion(dom, sid);//写入后删除内存中的cookie，下一次访问时再从磁盘读取（如果未过期）
		return true;
	}
}
String NetLibrary::HttpServer::ResolveValidSessionId(const char* dom, const Value& cookie_val)
{
	if (cookie_val.IsNull() || !dom || !*dom)
		return String();
	Value sid_val = cookie_val["seesion_id"];
	if (sid_val.IsNull())
		return String();
	// 单个 seesion_id 值（最常见情况）
	if (!sid_val.Is<ValueArray>())
	{
		String s = sid_val.ToString();
		if (!s.IsEmpty())
			return s;
		return String();
	}
	// 多个 seesion_id 值：逐一尝试匹配服务器存储
	ValueArray arr = sid_val;
	for (int i = 0; i < arr.GetCount(); i++)
	{
		String candidate = arr[i].ToString();
		if (candidate.IsEmpty())
			continue;
		// 先尝试从磁盘反序列化（如果存在），再查找内存
		cookie.DeserializeFromDisk(dom, candidate, cachefile);
		Cookie* co = cookie.FindSeeion(dom, candidate);
		if (co)
		{
			// 检查是否过期
			if (!CheckCookieExpiration(dom, candidate))
				return candidate;
			// 过期了，继续检查下一个候选
		}
	}
	// 没有匹配的，返回空字符串（调用者会创建新session）
	return String();
}
Value NetLibrary::HttpServer::GetCookie(const char* dom, const char* sid, const char* client_ip)
{
	if (!dom || !*dom) return Value();
	Cookie* co = nullptr;
	bool has_sid = sid && *sid;
	if (has_sid && RWCookie(dom, sid, false))
	{
		// 浏览器带了有效的 seesion_id 且未过期，直接使用
		co = cookie.FindSeeion(dom, sid);
	}
	else
	{
		// 没有有效 sid（新用户或已过期）
		// 先检查该域名下是否已有内存中的 cookie（并发请求已创建），复用之
		CookieBox* box = cookie[dom];
		if (box && client_ip && *client_ip)
			co = box->FindByClientIp(client_ip);
		if (!co)
		{
			// 确实不存在，创建新的
			if (!box)
				box = cookie.Add(dom);
			Uuid cid = Uuid::Create();
			String cidstr = cid.ToStringWithDashes();
			co = box->Add(cidstr);
			co->list.Add({ "seesion_id", cidstr });
			co->list.Add({ "max-age", IntStr64(cookie_expire_sec) });
			co->list.Add({ "expires", std::to_string(::GetUTCSeconds(::GetSysTime()) + cookie_expire_sec).c_str() });
			if (client_ip && *client_ip)
				co->list.Add({ "_client_ip", client_ip });
		}
	}
	if (!co)
		return Value();
	ValueMap vm;
	for (auto& a : co->list) {
		String k = a.key.c_str();
		if (k.StartsWith("_client_ip"))
			continue;
		String v = a.val.c_str();
		vm.GetAdd(k) = v;
	}
	return vm;
}
void NetLibrary::HttpServer::HandleDelUser(Uuid sid)
{
	// 先在临界区移除映射并采集需要的元数据，避免长时间持锁
	UserInfo* u = nullptr;
	String host;
	String cid;
	{
		Mutex::Lock __(lock);
		if (!sidtousers.Contains(sid))
			return;
		sidtousers.TryGetCopy(sid, u);
		// 移除相关定时任务（tasks 可能是线程安全，也可以在临界区移除）
		tasks.RemoveTaskIf([&u](const std::tuple<UserInfo*>& payload) {
			return std::get<0>(payload) == u;
		});
		// 提取 host，用于 cookie 序列化/删除
		if (headers.Find(sid) >= 0)
		{
			host = ~~headers.Get(sid)["host"];
			Value cookie_val = headers.Get(sid)["cookie"];
			if(!cookie_val.IsNull())
				cid = ResolveValidSessionId(~host, cookie_val);
		}
		headers.RemoveKey(sid);
		issended.RemoveKey(sid);
		// 从映射中立即移除，避免被其它线程再次使用
		this->DelUser(sid);
		RWCookie(host, cid, true);
	}
}
void NetLibrary::HttpServer::KeepAlive(UserInfo* u)
{
	if (!u) return;
	// 已经不在映射里，说明连接已经被其他路径关掉了
	if (!sidtousers.Contains(u->Getsid()) && !fdtousers.Contains(u->GetFd()))
		return;

	// 当前时间（你项目里已有 Upp 时间工具）
	int64 now = ::GetUTCSeconds(::GetSysTime());
	int64 last = u->GetPingPong();
	int64 diff = now - last;

	// 首次 / 异常值：认为尚未开始计时，重置并重新安排任务
	if (last <= 0 || diff < 0)
	{
		u->SetPingPong(now);
		return;
	}
	// 已空闲时间 >= keepalivesec：真正超时，关闭
	if (diff >= keepalivesec)
	{
		HandleDelUser(u->Getsid());
		return;
	}

	// 还没到超时时间：按剩余时间再排一次任务
	int remain = (int)(keepalivesec - diff);
	if (remain <= 0)
		remain = 1;
}
void NetLibrary::HttpServer::HandleFileSend(ProHttp& p, UserInfo* u, const char* filename)
{
	RLOG("HandleFileSend: enter file=" << (filename ? filename : "(null)") << " sid=" << u->Getsid().ToStringWithDashes() << " time=" << WwwFormat(GetUtcTime()));

	// 在临界区读取客户端请求头相关字段（避免并发访问 headers 导致的竞态）
	String accept_encoding;
	{
		Mutex::Lock __(lock);
		if (headers.Find(u->Getsid()) >= 0)
			accept_encoding = ~headers.Get(u->Getsid())["accept-encoding"];
	}

	splitfilezize = splitfilezize < 5 * 1024 * 1024 ? 5 * 1024 * 1024 : splitfilezize;
	bool client_accepts_gzip = accept_encoding.Find("gzip") > -1;

	if (FileExists(filename))
	{
		String ext = GetFileExt(filename);
		String mime = FileExtToMIME(ext);
		p.SetProtocolBody(Httplib::HttpProResponseField::Content_Type, mime);

		// 下面这些是耗时的 IO / CPU 操作，不能在持锁状态下执行
		FileIn in(filename);
		AssetLib::BinaryData filedata(0, GetFileLength(filename));
		RLOG("HandleFileSend: reading file size=" << filedata.size << " time=" << WwwFormat(GetUtcTime()));
		in.Get(filedata.data, filedata.size);
		RLOG("HandleFileSend: read complete, bytes=" << filedata.size << " time=" << WwwFormat(GetUtcTime()));

		// 仅对可压缩类型尝试 gzip
		auto is_compressible_mime = [](const String& m) -> bool {
			if (m.IsEmpty()) return false;
			std::string mm = m.ToStd();
			if (mm.rfind("text/", 0) == 0) return true;
			if (mm == "application/javascript" || mm == "application/json" ||
				mm == "application/xml" || mm == "application/xhtml+xml" ||
				mm == "text/css" || mm == "image/svg+xml")
				return true;
			return false;
		};

		bool isgzip = client_accepts_gzip && is_compressible_mime(mime);
		if (isgzip) {
			RLOG("HandleFileSend: GZIP start, bytes=" << filedata.size << " time=" << WwwFormat(GetUtcTime()));
			if (!Gzip(filedata)) {
				RLOG("HandleFileSend: GZIP failed, fallback identity time=" << WwwFormat(GetUtcTime()));
				isgzip = false;
			}
			else {
				RLOG("HandleFileSend: GZIP done, new size=" << filedata.size << " time=" << WwwFormat(GetUtcTime()));
			}
		}

		p.SetProtocolBody(Httplib::HttpProResponseField::Content_Encoding, isgzip ? "gzip" : "identity");
		p.SetProtocolBody(Httplib::HttpProResponseField::Content_Length, (int)filedata.size);

		RLOG("HandleFileSend: submit header time=" << WwwFormat(GetUtcTime()));
		SubmitWork(u, ~p); // 发送头部（非阻塞入队才安全）
		RLOG("HandleFileSend: header submitted time=" << WwwFormat(GetUtcTime()));

		if (filedata.size > splitfilezize)
		{
			int size = filedata.size;
			while (size > 0)
			{
				size_t chunk = size > splitfilezize ? splitfilezize : size;
				AssetLib::BinaryData spildata(0, chunk);
				in.Get(spildata.data, spildata.size);
				RLOG("HandleFileSend: submit chunk size=" << spildata.size << " time=" << WwwFormat(GetUtcTime()));
				this->SubmitWork(u, pick(spildata));
				size -= (int)chunk;
			}
			// 标记已发送完成
			Mutex::Lock __(lock);
			issended.Add(u->Getsid(), true);
		}
		else
		{
			RLOG("HandleFileSend: submit whole body size=" << filedata.size << " time=" << WwwFormat(GetUtcTime()));
			this->SubmitWork(u, pick(filedata));
			Mutex::Lock __(lock);
			issended.Add(u->Getsid(), true);
		}
	}
	else {
		RLOG("HandleFileSend: file not found: " << (filename ? filename : "(null)") << " time=" << WwwFormat(GetUtcTime()));
	}
}

void HttpServer::ProcessHttpRequest(One<SockPacket> pack, Value pro)
{
	UserInfo* user = nullptr;
	if (!fdtousers.TryGetCopy(pack->get_fd(), user) || !user)
		return;
	Uuid sid = Uuid::Create();
	user->Setsid(sid);
	// 仅在临界区修改共享容器：sidtousers, headers, issended 等
	{
		Mutex::Lock __(lock);
		sidtousers.InsertOrAssign(sid, user);
		headers.Add(sid, pro);
	}
	// 设置 last-active 与排 KeepAlive 任务放在临界区外（避免持锁调用任务系统）
	user->SetPingPong(::GetUTCSeconds(::GetSysTime()));
	if (iskeepalive)
		tasks.CreateTask({ this, &HttpServer::KeepAlive }, keepalivesec, user);
	// 触发外部事件（不要在持锁时调用外部回调）
	assetEventBus.Emit("Requests", ~pack->tostring(), ~user->Getsid().ToStringWithDashes());
}

void NetLibrary::HttpServer::CreatBaseResponseHead(ProHttp& p, const char* sid, Httplib::StatusCode code, const char* ext, njson bodyex)
{
	p.SetResponseHead(code);
	p.SetProtocolBody(Httplib::HttpProResponseField::Content_Type, code == Httplib::StatusCode::NotFound_404 ? "text/html;" : ext);
	p.SetProtocolBody(Httplib::HttpProResponseField::Connection, iskeepalive && !IsCloseStatus(code) ? "keep-alive" : "close");
	p.SetProtocolBody(Httplib::HttpProResponseField::Content_Language, "zh-CN");
	p.SetProtocolBody(Httplib::HttpProResponseField::Date, WwwFormat(GetUtcTime()));
	p.SetProtocolBody(Httplib::HttpProResponseField::Server, "NetLibrary.HttpServer");
	{
		String host_str;
		String cookie_sid_str;
		String client_ip_str;
		{
			Mutex::Lock __(lock);
			Uuid parsed_sid = ::ScanUuid(sid);
			if(headers.Find(parsed_sid) >= 0)
			{
				ProHttp& req = headers.Get(parsed_sid);
				host_str = ~~req["host"];
				Value cookie_val = req["cookie"];
				if(!cookie_val.IsNull())
					cookie_sid_str = ResolveValidSessionId(~host_str, cookie_val);
			}
			// 获取客户端IP用于区分不同客户端
			UserInfo* u = nullptr;
			if (sidtousers.TryGetCopy(parsed_sid, u) && u)
				client_ip_str = inet_ntoa(u->GetclientAddr().sin_addr);
		}
		if(!host_str.IsEmpty())
		{
			Value ck = GetCookie(~host_str, ~cookie_sid_str, ~client_ip_str);
			if(!ck.IsNull() && !ck.IsError())
				p.SetProtocolBody(Httplib::HttpProResponseField::Set_Cookie, ck);
		}
	}
	if (!bodyex.is_null())//插入外部扩展的字段
		p.SetProtocolBody(bodyex);
}

void HttpServer::EventFun(EventType etype, One<SockPacket>& obj, Value val)
{
	One<SockPacket> pack = pick(obj);
	SOCKET fd = pack->get_fd();
	switch (etype) {
	case EventType::SENDCACHENULL: //写缓冲满了，将客户端的epoll修改为包含写完成事件
#ifdef PLATFORM_POSIX
	{
		// 需要获取 user 对象，短锁读取后释放
		UserInfo* user = nullptr;
		{
			Mutex::Lock __(lock);
			fdtousers.TryGetCopy(fd, user);
		}
		if (user) {
			SubmitWork(pick(pack), false, false);
			SetEpoll(user->GetFd(), EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLOUT, EPOLL_CTL_MOD);
		}
	}
#endif // PLATFORM_POSIX
	break;
	case EventType::SENDENDEVENT://缓冲数据写完，改变sock侦听状态
	{
		if (val.Is<bool>() && !val.Get<bool>())
		{
			TcpServer::EventFun(etype, pack, val);
			return;
		}
		UserInfo* user = nullptr;
		{
			Mutex::Lock __(lock);
			fdtousers.TryGetCopy(fd, user);
		}
		if (user) {
			ProHttp pro = pack->tostring();
			bool isclosed = pro[Httplib::HttpProResponseField::ToString((int)Httplib::HttpProResponseField::Connection)] == "close";
			bool shouldDel = false;
			{
				Mutex::Lock __(lock);
				if (issended.GetCount() > 0 && issended.Find(user->Getsid()) >= 0 && issended.Get(user->Getsid()) && !iskeepalive)
					shouldDel = true;
				if (!iskeepalive)
					shouldDel = true;
			}
			if (isclosed || shouldDel) {
				// HandleDelUser 内部已包含 RWCookie 写入逻辑，无需重复调用
				HandleDelUser(user->Getsid());
			}
		}
	}
		break;
	case EventType::ERROREVENT://接收数据错误事件
	{
		UserInfo* user = nullptr;
		{
			Mutex::Lock __(lock);
			fdtousers.TryGetCopy(fd, user);
		}
		if (user) {
			SubmitWork(pick(pack));
			HandleDelUser(user->Getsid());
		}
	}
		break;
	case EventType::HTTPREQUEST://接收到http请求事件，开始处理http请求
	{
		ProcessHttpRequest(pick(pack), val);
	}
		break;
	case EventType::DATAEVENT://接收到完整数据，开始处理http结构化信息
	{
		TcpServer::EventFun(etype, pack, val);
	}
		break;
	case EventType::MEMRETEVENT://内存空闲块被归还事件
		queue.SignalQueue();
		break;
	default:
		
		break;
	}
}

void NetLibrary::HttpServer::BufferQueue(DiskTask* s)
{
	UserInfo* u = nullptr;
	{
		Mutex::Lock __(lock);
		fdtousers.TryGetCopy(s->fd, u);
	}
	if (IsAnyMem(s->data_size) && u)
	{
		if (s->istemp)
		{
			AssetLib::BinaryData d;
            FileIn in(s->temp_file_path);
			d.size = in.GetSize();
			in.Get(d.data, d.size);
			SendData(u, s->code, ::GetExeDirFile(s->temp_file_path), pick(d), s->body);
			FileDelete(s->temp_file_path);
		}	
		else
			SendData(u,s->code, s->temp_file_path, s->body);
	}
}

void NetLibrary::HttpServer::ServerIsbusy(SOCKET fd)
{
	ProHttp p;
	p.SetResponseHead(Httplib::StatusCode::ServiceUnavailable_503);
	p.SetProtocolBody(Httplib::HttpProResponseField::Content_Type, "text/html;");
	p.SetProtocolBody(Httplib::HttpProResponseField::Connection, "close");
	p.SetProtocolBody(Httplib::HttpProResponseField::Content_Language, "zh-CN");
	p.SetProtocolBody(Httplib::HttpProResponseField::Date, WwwFormat(GetUtcTime()));
	p.SetProtocolBody(Httplib::HttpProResponseField::Server, "NetLibrary.HttpServer");
	String body = "<html><head><title>503 Service Unavailable</title></head><body><h1>503 Service Unavailable</h1><p>The server is currently unable to handle the request due to a temporary overload or scheduled maintenance. Please try again later.</p></body></html>";
	p.SetProtocolBody(Httplib::HttpProResponseField::Content_Length, body.GetLength());
	String data = ++p;
	send(fd, data, data.GetLength(), 0);
}

String NetLibrary::HttpServer::ResolvePath(const char* path)
{
	if (!path) return String();
	String p = NormalizePath(path);

	// 若是已存在的文件或目录，直接返回规范化路径
	if (DirectoryExists(p) || FileExists(p))
		return p;

	// 尝试当前工作目录
	String cwd = GetCurrentDirectory();
	String alt = NormalizePath(AppendFileName(cwd, p));
	if (DirectoryExists(alt) || FileExists(alt))
		return alt;

	// 尝试可执行文件目录
	String exe = GetExeFolder();
	String alt2 = NormalizePath(AppendFileName(exe, p));
	if (DirectoryExists(alt2) || FileExists(alt2))
		return alt2;

	// 不存在：判断是目录还是文件（依据末尾是否为分隔符/是否有扩展名等）
	bool looks_dir = p.EndsWith("\\") || p.EndsWith("/")
	                 || GetFileExt(p).IsEmpty();

	String target = p;
	// 若非绝对路径，优先落地到可执行目录
	if (!IsFullPath(p))
		target = NormalizePath(AppendFileName(exe, p));

	if (looks_dir) {
		// 创建目录（递归）
		if (!DirectoryExists(target)) {
			if (!RealizeDirectory(target)) {
				RLOG("ResolvePath: failed to create directory: " << target);
			}
		}
		return target;
	}
	else {
		// 文件：为其父目录兜底创建
		String parent = GetFileDirectory(target);
		if (!DirectoryExists(parent)) {
			if (!RealizeDirectory(parent)) {
				RLOG("ResolvePath: failed to create parent directory: " << parent);
			}
		}
		return target;
	}
}

bool NetLibrary::HttpServer::IsCloseStatus(Httplib::StatusCode code)
{
    switch (code)
    {
    // 客户端请求错误，通常关闭
    case NetLibrary::Httplib::StatusCode::BadRequest_400:
    case NetLibrary::Httplib::StatusCode::Unauthorized_401:
    case NetLibrary::Httplib::StatusCode::PaymentRequired_402:
    case NetLibrary::Httplib::StatusCode::Forbidden_403:
    case NetLibrary::Httplib::StatusCode::NotFound_404:
    case NetLibrary::Httplib::StatusCode::MethodNotAllowed_405:
    case NetLibrary::Httplib::StatusCode::NotAcceptable_406:
    case NetLibrary::Httplib::StatusCode::ProxyAuthenticationRequired_407:
    case NetLibrary::Httplib::StatusCode::RequestTimeout_408:
    case NetLibrary::Httplib::StatusCode::Conflict_409:
    case NetLibrary::Httplib::StatusCode::Gone_410:
    case NetLibrary::Httplib::StatusCode::LengthRequired_411:
    case NetLibrary::Httplib::StatusCode::PreconditionFailed_412:
    case NetLibrary::Httplib::StatusCode::PayloadTooLarge_413:
    case NetLibrary::Httplib::StatusCode::UriTooLong_414:
    case NetLibrary::Httplib::StatusCode::UnsupportedMediaType_415:
    case NetLibrary::Httplib::StatusCode::RangeNotSatisfiable_416:
    case NetLibrary::Httplib::StatusCode::ExpectationFailed_417:
    case NetLibrary::Httplib::StatusCode::MisdirectedRequest_421:
    case NetLibrary::Httplib::StatusCode::UnprocessableContent_422:
    case NetLibrary::Httplib::StatusCode::Locked_423:
    case NetLibrary::Httplib::StatusCode::FailedDependency_424:
    case NetLibrary::Httplib::StatusCode::TooEarly_425:
    case NetLibrary::Httplib::StatusCode::UpgradeRequired_426:
    case NetLibrary::Httplib::StatusCode::PreconditionRequired_428:
    case NetLibrary::Httplib::StatusCode::TooManyRequests_429:
    case NetLibrary::Httplib::StatusCode::RequestHeaderFieldsTooLarge_431:
    case NetLibrary::Httplib::StatusCode::UnavailableForLegalReasons_451:
        return true;

    // 服务端错误，通常关闭
    case NetLibrary::Httplib::StatusCode::InternalServerError_500:
    case NetLibrary::Httplib::StatusCode::NotImplemented_501:
    case NetLibrary::Httplib::StatusCode::BadGateway_502:
    case NetLibrary::Httplib::StatusCode::ServiceUnavailable_503:
    case NetLibrary::Httplib::StatusCode::GatewayTimeout_504:
    case NetLibrary::Httplib::StatusCode::HttpVersionNotSupported_505:
    case NetLibrary::Httplib::StatusCode::VariantAlsoNegotiates_506:
    case NetLibrary::Httplib::StatusCode::InsufficientStorage_507:
    case NetLibrary::Httplib::StatusCode::LoopDetected_508:
    case NetLibrary::Httplib::StatusCode::NotExtended_510:
    case NetLibrary::Httplib::StatusCode::NetworkAuthenticationRequired_511:
        return true;

    // 1xx/2xx/3xx：一般保持连接
    default:
        return false;
    }
}

void NetLibrary::HttpServer::StopHttpServer()
{
	this->ShutDown();
	cookie.Clear();
	headers.Clear();
	queue.Stop();
}

void NetLibrary::HttpServer::StartHttpServer(const char* conf)
{
	Value map = ParseJSON(conf);
	if (map.IsError() || map.IsNull())
	{
		assetEventBus.Emit(SYSEVENT::SYSERR, "Configuration file parsing failed.");
		return;
	}
	isssl = map["isssl"].Is<bool>() ? map["isssl"].Get<bool>() : map["isssl"] == "true" ? true : false;
	iskeepalive = map["iskeepalive"].Is<bool>() ? map["iskeepalive"].Get<bool>() : map["iskeepalive"] == "true" ? true : false;
	listenaddr = map["listenaddr"];
	listenport = map["listenport"].Is<double>() ? (int)map["listenport"].Get<double>() : ::StrInt(~~map["listenport"]);
	keepalivesec = map["keepalivesec"].Is<double>() ? (int)map["keepalivesec"].Get<double>() : ::StrInt(~~map["keepalivesec"]);
	concurrency = map["concurrency"].Is<double>() ? (int)map["concurrency"].Get<double>() : ::StrInt(~~map["concurrency"]);
	cachesize = map["cachesize"].Is<double>() ? (int)map["cachesize"].Get<double>() : ::StrInt(~~map["cachesize"]);
	splitfilezize = (map["splitfilezize"].Is<double>() ? (int)map["splitfilezize"].Get<double>() : ::StrInt(~~map["splitfilezize"])) * 1024;
	cachefile = map["cachefile"].IsNull() ? GetExeFolder() : ResolvePath(~map["cachefile"]);
	cookie_expire_sec = map["cookie_expire_sec"].Is<double>() ? (int64)map["cookie_expire_sec"].Get<double>() : ::StrInt64(~~map["cookie_expire_sec"]);
	// 安全校验与初始化 SSL
	if (isssl)
	{
		String cert = ~map["certificate"];
		String pkey = ~map["privatekey"];

		// 判断为空、是否可访问
		if (IsVoid(cert) || IsVoid(pkey) || !FileExists(cert) || !FileExists(pkey))
		{
			assetEventBus.Emit(SYSEVENT::SYSERR, "SSL certificate or private key is missing or invalid.");
			isssl = false; // 降级为非 SSL 模式
		}
		else
		{
			if (!InitSSL(cert, pkey))
			{
				assetEventBus.Emit(SYSEVENT::SYSERR, "SSL initialization failed.");
				isssl = false; // 降级为非 SSL 模式
			}
		}
	}

	try {
		this->StartServer(listenaddr, listenport, concurrency, cachesize);
	}
	catch (NetException& e) {
		if(e.getErrType() == ExcType::SOCKETERR || e.getErrType() == ExcType::EPOLLERROR)
		{
			this->ShutDown();
			assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to create socket or epoll instance.");
		}
		else
			assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to start HTTP server.");
		assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to create cache directory.");
		return;
	}
}
void NetLibrary::HttpServer::On(EMIEVENT ev, AssetLib::EventHandler&& fun)
{
	assetEventBus.On(ev, std::move(fun));
}
void NetLibrary::HttpServer::Emit(EMIEVENT ev, AssetLib::BinaryData data, bool isBinary)
{
	String sev = ev ? ev : "";
	HttpData hd;
	AssetLib::SerializerFactory::Deserialize(hd, data);
	UserInfo* u = nullptr;
	// 只在临界区查找 sid->fd 映射，避免并发读取竞态
	{
		Mutex::Lock __(lock);
		Uuid emit_sid = ScanUuid(hd.sid.c_str());
		if (!sidtousers.TryGetCopy(emit_sid, u) || !u)
			return;
	}
	if (sev == "respond")
	{
		njson bodyex = njson::parse(hd.bodyex, nullptr, true, false, true);
		if (hd.isfile)
			SendData(u, hd.code, hd.data.c_str(), bodyex);
		else
			SendData(u, hd.code, hd.codeex.c_str(), hd.data.c_str(), bodyex);

		RLOG("Emit: respond end, time=" << WwwFormat(GetUtcTime()));
		return;
	}
	if(sev == "redirect")
	{
		Redirect(hd.sid.c_str(), hd.data.c_str(), hd.isfile);
	}
	if(sev == "addcookie")
	{
		ProHttp pro;
		String host;
		{
			Mutex::Lock __(lock);
			if(headers.Find(ScanUuid(hd.sid.c_str())) == -1)
				return;
			host = headers.Get(ScanUuid(hd.sid.c_str()))["host"];
		}
		Value covalue = ParseJSON(hd.data.c_str());
		if (covalue.Is<ValueMap>())
		{
			ValueMap cmap = covalue;
			for (int i = 0; i < cmap.GetCount(); i++)
				AddCookie(host, hd.sid.c_str(), ~~cmap.GetKey(i), ~~cmap.GetValue(i));
			return;
		}
	}
}
void HttpServer::SendData(UserInfo* u, Httplib::StatusCode code, const char* filename, njson bodyex)
{
	// 先快查 fdtousers 与 headers，且不要在持锁时执行后续 IO
	bool needDel = false;
	Uuid sidToDel;
	{
		Mutex::Lock __(lock);
		if (headers.Find(u->Getsid()) == -1)
		{
			// 若 headers 丢失，标记稍后删除
			needDel = true;
			sidToDel = u->Getsid();
		}
	}
	if (needDel)
	{
		HandleDelUser(sidToDel);
		return;
	}

	// 构造响应头（CreatBaseResponseHead 不应修改共享容器），该过程可以在无锁下进行
	size_t filesize = 0;
	if (!this->IsAnyMem((filesize = FileIn(filename).GetSize())))
	{
		queue.CreatQueue(THISFN(BufferQueue), false, u->GetFd(), code, filename, filesize, false, bodyex);
		return;
	}
	ProHttp header;
	CreatBaseResponseHead(header, u->Getsid().ToStringWithDashes(), code, FileExtToMIME(GetFileExt(filename)), bodyex);
	HandleFileSend(header, u, filename);
}

bool NetLibrary::HttpServer::AddCookie(const char* dom, const char* sid, const char* key, const char* val)
{
	if (!dom || !sid || !key || !val) return false;
	Cookie* co = cookie.FindSeeion(dom, sid);
	if (!co)
		return false;
	co->list.Add({key,val});
	return true;
}

void NetLibrary::HttpServer::Redirect(const char* sid, const char* dom, bool isSLL)
{
	if (!sid || !dom) return;
	Uuid parsed_sid = ScanUuid(sid);
	UserInfo* u = nullptr;
	{
		Mutex::Lock __(lock);
		if (!sidtousers.TryGetCopy(parsed_sid, u) || !u)
			return;
	}
	if (!u) return;
	ProHttp header;
	header.SetResponseHead(Httplib::HttpProStatus::MovedPermanently_301);
	header.SetProtocolBody(Httplib::HttpProResponseField::Location, String("https://") << dom);
	header.SetProtocolBody(Httplib::HttpProResponseField::Strict_Transport_Security, "max-age=31536000; includeSubDomains; preload");
	this->SubmitWork(u, ++header);
}

void NetLibrary::HttpServer::SendData(UserInfo* u, Httplib::StatusCode code, const char* ext, AssetLib::BinaryData d, njson bodyex)
{
	bool needDel = false;
	Uuid sidToDel;
	{
		Mutex::Lock __(lock);
		if (headers.Find(u->Getsid()) == -1)
		{
			// 若 headers 丢失，标记稍后删除
			needDel = true;
			sidToDel = u->Getsid();
		}
	}
	if (needDel)
	{
		HandleDelUser(sidToDel);
		return;
	}
	if (!this->IsAnyMem(d.size))
	{
		int hash = xxHash(d.data, d.size);
		String filename = cachefile << "/temp/" << Format("%d.%s", hash, ext);
		FileOut out(filename);
		out.Put(d.data, d.size);
		queue.CreatQueue(THISFN(BufferQueue), false, u->GetFd(), code, filename, d.size, true, bodyex);
		return;
	}
	ProHttp header;
	CreatBaseResponseHead(header, u->Getsid().ToStringWithDashes(), code, ext, bodyex);
	String ae;
	{
		Mutex::Lock __(lock);
		if (headers.Find(u->Getsid()) >= 0)
			ae = ~~headers.Get(u->Getsid())["accept-encoding"];
	}
	if (ae.Find("gzip") > -1 && Gzip(d))
		header.SetProtocolBody(Httplib::HttpProResponseField::Content_Encoding, "gzip");
	else
		header.SetProtocolBody(Httplib::HttpProResponseField::Content_Encoding, "identity");
	header.SetProtocolBody(Httplib::HttpProResponseField::Content_Length, (int)d.size);
	String ss = ~header;
	this->SubmitWork(u, ~ss);
	if(d)
		this->SubmitWork(u, pick(d));
}

