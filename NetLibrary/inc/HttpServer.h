#ifndef _NetLibrary_HttpServer_h_
#define _NetLibrary_HttpServer_h_
#include "httpdef.h"
namespace NetLibrary {
class HttpServer : private TcpServer,public Httplib::HttpSvrInterface {
	typedef HttpServer CLASSNAME;
private:
	Mutex lock;
	Value config;
	CookieList cookie;
	VectorMap<Uuid, ProHttp> headers;
	AssetLib::ThreadQueue<DiskTask, void> queue;
	/// <summary>
	/// 记录发送情况，int64为最后一个数据的指针值
	/// </summary>
	VectorMap<Uuid, bool> issended;
	/// <summary>
	/// 是否https状态
	/// </summary>
	bool isssl = false;
	/// <summary>
	/// 是否启用长连接
	/// </summary>
	bool iskeepalive = false;
	/// <summary>
	/// 侦听地址，默认为本机地址
	/// </summary>
	String listenaddr = "localhost";
	/// <summary>
	/// 侦听端口, 默认为80端口
	/// </summary>
	int listenport = isssl ? 443 : 80;
	/// <summary>
	/// keepalive 默认超时秒数60
	/// </summary>
	int keepalivesec = 60;
	/// <summary>
	/// 最大并发数量，默认100 超出直接关闭连接
	/// </summary>
	int concurrency = 100;
	/// <summary>
	/// 最大缓存之，默认0 由系统根据实际情况自动分配
	/// </summary>
	int64 cachesize = 0;
	/// <summary>
	/// 文件发送的分包大小,默认超过5m 分包
	/// </summary>
	size_t splitfilezize = 5*1024*1024;
	/// <summary>
	/// 缓存目录地址
	/// </summary>
	String cachefile = GetExeFolder();
	/// <summary>
	/// 表示 cookie 过期时间（以秒为单位）的 64 位整数变量，默认值为 3600（1 小时）。
	/// </summary>
	int64 cookie_expire_sec = 3600;
private:
	/// <summary>
	/// 启动数据gzip压缩
	/// </summary>
	/// <param name="zipbuf">原始的数据对象（成功压缩后变为被压缩数据对象）</param>
	/// <returns>返回成功或失败</returns>
	bool Gzip(AssetLib::BinaryData& zipbuf);
	bool CheckCookieExpiration(const char* dom, const char* sid);
	bool RWCookie(const char* dom, const char* sid, bool isWrite);
	/// <summary>
	/// 获取指定域和标识符对应的 Cookie。
	/// </summary>
	/// <param name="dom">指向表示域（domain）的以 null 结尾的 C 字符串。</param>
	/// <param name="sid">指向表示 Cookie 标识符或会话 ID 的以 null 结尾的 C 字符串。</param>
	/// <param name="client_ip">指向表示客户端 IP 的以 null 结尾的 C 字符串（可选）。</param>
	/// <returns>返回表示所请求 Cookie 的 Value 对象；如果未找到，通常返回空或默认构造的 Value（具体行为取决于 Value 类型的实现）。</returns>
	Value GetCookie(const char* dom, const char* sid, const char* client_ip = nullptr);
	/// <summary>
	/// 从请求cookie中解析出有效的seesion_id（防御浏览器发送多个seesion_id的情况）
	/// </summary>
	/// <param name="dom">域名</param>
	/// <param name="cookie_val">解析后的cookie Value对象</param>
	/// <returns>匹配服务器存储的有效seesion_id，未找到则返回空字符串</returns>
	String ResolveValidSessionId(const char* dom, const Value& cookie_val);
	/// <summary>
	/// 删除用户
	/// </summary>
	/// <param name="sid">用户的seesionid值</param>
	void HandleDelUser(Uuid sid);
	/// <summary>
	/// 长周期连接超时退出方法
	/// </summary>
	/// <param name="u">用户对象</param>
	void KeepAlive(UserInfo* u);
	/// <summary>
	/// 执行文件发送
	/// </summary>
	/// <param name="p">头文件对象</param>
	/// <param name="u">存储socket连接用户的对象</param>
	/// <param name="filename">文件完整的路径和名称</param>
	void HandleFileSend(ProHttp& p, UserInfo* u, const char* filename);
	/// <summary>
	/// 解析http请求数据
	/// </summary>
	/// <param name="pack">数据包</param>
	void ProcessHttpRequest(One<SockPacket> pack, Value pro);
	/// <summary>
	/// 创建基础响应头
	/// </summary>
	/// <param name="p">头对象</param>
	/// <param name="sid">seesion id</param>
	/// <param name="code">响应状态</param>
	/// <param name="ext">响应文件类型</param>
	/// <param name="bodyex">扩展body字段</param>
	void CreatBaseResponseHead(ProHttp& p,const char* sid, Httplib::StatusCode code, const char* ext, njson bodyex);

	/// <summary>
	/// 缓存队列，主要用于已无足够内存完成处理而设计
	/// </summary>
	/// <param name="s"></param>
	void BufferQueue(DiskTask* s);

	void ServerIsbusy(SOCKET fd);
	String ResolvePath(const char* path);
	bool IsCloseStatus(Httplib::StatusCode code);
protected:
	/// <summary>
	/// socket消息事件，用于接收各种消息的方法
	/// </summary>
	/// <param name="etype">事件的类型</param>
	/// <param name="obj">包对象</param>
	/// <param name="val">附加值</param>
	virtual void EventFun(EventType etype, One<SockPacket>& obj, Value val) override;
	void SendEvent(Uuid uid, EMIEVENT ev, AssetLib::BinaryData d, bool isBinary) = delete;
	void StopHttpServer();
	void StartHttpServer(const char* confpath);
	/// <summary>
/// 向特定seesion用户发送http数据（以文件形式）
/// </summary>
/// <param name="sid">用户的seesionid</param>
/// <param name="filename">需要发送的文件</param>
/// <param name="bodyex">扩展的字段内容</param>
	void SendData(UserInfo* u, Httplib::StatusCode code, const char* filename, njson bodyex);
	/// <summary>
	/// 向特定seesion用户发送http数据（以数据形式）
	/// </summary>
	/// <param name="sid">用户的seesionid</param>
	/// <param name="code">响应类型</param>
	/// <param name="ext">响应的MMIE类型</param>
	/// <param name="d">数据</param>
	/// <param name="bodyex">扩展字段内容</param>
	void SendData(UserInfo* u, Httplib::StatusCode code, const char* ext, AssetLib::BinaryData d, njson bodyex);
public:
	HttpServer()
	{
		TcpServer::Whenbusy << THISFN(ServerIsbusy);
	}
	virtual void ShutDownNetSver() override
	{
		StopHttpServer();
	}
	virtual void StartNetSver(const char* host, int port, int unum, size_t memsize) override
	{
	}
	virtual void StartNetSver(const char* conf) override
	{
		StartHttpServer(conf);
	}
	virtual void On(EMIEVENT ev, AssetLib::EventHandler&& fun) override;
	virtual void Emit(EMIEVENT ev, AssetLib::BinaryData data, bool isBinary = false) override;
	virtual void DisconnectClient(const char* sid) override
	{
		HandleDelUser(ScanUuid(sid));
	}
	/// <summary>
	/// 添加cookie的方法
	/// </summary>
	/// <param name="dom">域名</param>
	/// <param name="sid">seesionid值</param>
	/// <param name="key">键</param>
	/// <param name="val">值</param>
	bool AddCookie(const char* dom,const char* sid,const char* key,const char* val);
	/// <summary>
	/// 重定向
	/// </summary>
	/// <param name="sid">seesion id</param>
	/// <param name="dom">重定向域名</param>
	/// <param name="isSLL">重定向是否是https服务器</param>
	void Redirect(const char* sid, const char* dom, bool isSLL = false);

	virtual ~HttpServer() noexcept override {
		StopHttpServer();
	}
};
};
#endif
