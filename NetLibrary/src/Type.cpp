#include "../inc/include.h"

namespace NetLibrary {

	AssetLib::EventBus<> assetEventBus;
	const char* MessageDesc(MsgType type, MsgCode code, int value)
	{
		return MessageDesc(type, (int)code, value);
	}

	const char* MessageDesc(MsgType type, int code, int value)
	{
		int comment = (int)type << 10 & code;
		static Tuple<int, const char*> msg[] = {
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_CREATED,                 Format("SOCKET已存在，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_FLAG,                 Format("SOCKET标志错误，没有该标志类型的socket,错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_DNS,                 Format("域名或IP解析发生错误，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EACCES,                 Format("权限被拒绝，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EADDRINUSE,                 Format("地址已在使用，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EADDRNOTAVAIL,                 Format("地址不可用，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EAFNOSUPPORT,                 Format("地址族不支持，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & 11,                 Format("资源暂时不可用，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EBADF,                 Format("无效的文件描述符，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ECONNABORTED,                 Format("软件引起的连接中止，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ECONNREFUSED,                 Format("连接被拒绝，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ECONNRESET,                 Format("连接被对方重置，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EDESTADDRREQ,                 Format("需要目标地址，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EFAULT,                 Format("系统调用参数指向了无效的内存地址，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EHOSTUNREACH,                 Format("主机不可达，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EINPROGRESS,                 Format("操作正在进行中，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EINTR,                 Format("系统调用被信号中断，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EINVAL,                 Format("无效的参数，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EISCONN,                 Format("传输端点已连接，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ELOOP,                 Format("遇到符号链接循环，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EMFILE,                 Format("进程打开文件数达到上限，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EMSGSIZE,                 Format("消息大小超出限制，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENETDOWN,                 Format("网络接口已关闭，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENETUNREACH,                 Format("网络不可达，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENFILE,                 Format("系统打开文件总数达到上限，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENOBUFS,                 Format("内存不足，系统缺乏足够的缓冲区资源，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENOMEM,                 Format("内存不足，系统缺乏足够的缓冲区资源，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENODEV,                 Format("找不到设备，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENOENT,                 Format("文件或目录不存在，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENOPROTOOPT,                 Format("协议层不认识此选项，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENOTCONN,                 Format("传输端点未连接，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ENOTSOCK,                 Format("描述符不是套接字，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EOPNOTSUPP,                 Format("操作不支持，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EPERM,                 Format("操作不被允许，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EPIPE,                 Format("管道破裂，会触发SIGPIPE信号（默认终止进程），错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EPROTO,                 Format("协议错误，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EPROTONOSUPPORT,                 Format("协议类型不支持，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_EROFS,                 Format("文件系统只读，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ESOCKTNOSUPPORT,                 Format("套接字类型在该地址族中不支持，错误代码：0x%x", comment) },
			{ (int)MsgType::SOCKET << 10 & (int)MsgCode::SOCKET_ERR_ETIMEDOUT,                 Format("连接超时，错误代码：0x%x", comment) },
		};
		const Tuple<int, const char*>* x = FindTuple(msg, __countof(msg), comment);
		return x ? x->b : NULL;
	}
	void PrintPut(const String& str)
	{

	}
	Value ParseValue(const Value& v)
	{
		if (v.Is<int>())
			return IntStr(v.Get<int>());
		if (v.Is<double>())
			return DblStr(v.Get<double>());
		if (v.Is<String>())
			return v.Get<String>();
		if (v.Is<bool>())
			return v.Get<bool>() ? "true" : "false";
		if (v.Is<float>())
			return DblStr(v.Get<float>());
		if (v.Is<int64>())
			return IntStr((int)v.Get<int64>());
		if (v.Is<WString>())
			return v.Get<WString>().ToString();
		if (v.Is<Date>())
			return Format("%04d-%02d-%02d", v.Get<Date>().year, v.Get<Date>().month, v.Get<Date>().day);
		if (v.Is<Time>())
		{
			Time t = v.Get<Time>();
			return Format("%04d-%02d-%02d %02d:%02d:%02d", t.year, t.month, t.day, t.hour, t.minute, t.second);
		}
		return Nuller();
	}

	static std::atomic<bool> g_log_inited{ false };

	NETLIB_API void NetLibrary::InitUppLogging(const char* logfile, bool enableConsole)
	{
		bool expected = false;
		if (!g_log_inited.compare_exchange_strong(expected, true)) {
			return; // 已初始化，避免重复
		}

		//SetDefaultCharset(CHARSET_UTF8);

#ifdef _WIN32
		SetLanguage(LNG_('Z', 'H', 'C', 'N'));
		SetDefaultCharset(CHARSET_UTF8);
#endif

		int flags = LOG_FILE;
		if (enableConsole)
			flags |= LOG_COUT;

		StdLogSetup(flags, logfile ? logfile : nullptr);

		LOG("NetLibrary logging initialized. console=" << enableConsole
			<< ", file=" << (logfile ? logfile : "(default)"));
	}

	// 修改：当 T 是 HttpSvrInterface 时，创建 HttpServer；否则默认创建 T
	NETLIB_API NetInterface* NetLibrary::CreateNetLibrary(bool isserve)
	{
		if(isserve)
		{
			auto* http = new NetLibrary::NetServer();
			return static_cast<NetInterface*>(http); // HttpServer 应实现/继承 HttpSvrInterface
		}
		return nullptr;
	}
	NETLIB_API Httplib::HttpSvrInterface* Httplib::CreateHttpSvrInterface(const char* conf)
	{
		auto* http = new NetLibrary::HttpServer();
		return static_cast<HttpSvrInterface*>(http);
	}

} // namespace NetLibrary


