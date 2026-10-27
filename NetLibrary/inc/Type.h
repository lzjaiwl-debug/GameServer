#ifndef _NetLibrary_Type_h_
#define _NetLibrary_Type_h_
namespace NetLibrary {
#define SID_LENGTH 20
#define SYSLOG(code,lpv,str) LOG(Format("[%`]Error:%s.Code:%d, Address:%d.", GetSysTime(), str,code,lpv));
	/// <summary>
	/// 外部声明的全局资产事件总线。
	/// </summary>
	extern AssetLib::EventBus<> assetEventBus;

	/// <summary>
	/// 用于对Uuid进行哈希的结构体
	/// </summary>
	struct UuidHash {
		size_t operator()(const Uuid& u) const noexcept {
			return u.GetHashValue(); // 或你项目里等价哈希函数
		}
	};
	//协议类型的定义

	enum class ProtocolType {
		CONNECT = 0,   // 已链接但未明确协议类型
		WEBSOCKET = 1, // 握手成功，建立的为websocket连接
		SOCKETIO = 2,  // 握手成功，使用SOCKETIO协议
		SOCKET = 3, // 握手成功，建立的为普通tcp连接
		HTTPSOCKET = 4,// HTTPSOCKET协议
		DECONNECT, // 表示没有任何连接
	};
	// SSL 状态枚举
	enum class SSLState {
		NONE,           // 未启用 SSL
		HANDSHAKING,    // 握手进行中
		CONNECTED,      // 握手完成，可正常通信
		SHUTDOWN,       // 正在关闭
		SSLERROR,       // 发生错误
	};
	enum class SockType {
		NONE,
		SERVER,
		CLIENT,
	};

	enum class EventType {
		DATAEVENT,//完整数据到达事件(无协议，纯文本)
		PRODATAEVENT,//协议数据到达事件(包含socket、websocket、socketio等协议)
		SIODATAEVENT,//socketio数据到达事件
		MEMRETEVENT,//内存空闲块归还事件
		SENDENDEVENT,//发送结束事件
		SESSIONEVENT,//设置session值事件
		SOCKETHANDSHAKE,//Socket握手事件
		PINGPONG,//心跳包事件
		CLOSEDEVENT,//客户端断开连接
		ERROREVENT,//接收数据错误
		SENDCACHENULL,//发送缓冲区为空
		SOCKETIOOPEN,//SocketIO open 事件
		SOCKETCONNECT,//
		SOCKETIOPOLL,//SocketIO轮询事件
		SOCKETIOHANDSHAKE,
		SSLHANDSHAKE,//
		HTTPREQUEST,// 
		HTTPRESPONSE,//
		HTTPCHUNKED,//
		NONE,//无事件
	};
	enum class SockErrType {
		SETOPT,
		CREAT,
		BIND,
	};
	enum class PacketType {
		NONE,
		SEND,
		RECV,
		SSL_SEND,//SSL数据发送事件
		SSL_RECV,//SSL数据接受事件
	};
	enum class MsgType {
		SOCKET = 0x100,
		EPOLL = 0x101,
	};
	enum class OPCODE
	{
		ISBLOCK = 0x00,//分片数据
		TEXTONLY = 0x01,//标识数据类型为纯文本
		BINARY = 0x02,//标识数据类型为二进制
		ISLAST = 0x03,//代表分片数据的最后一个数据
		ISWEBTOP = 0X04,//用于发送消息中判断是否发送的是web初始握手消息
		ISTXTCON = 0x05,//标识文本连续数据的开头
		ISBINARYCON = 0x06,//标识二进制连续数据的开头
		NONE = 0x0F,//代表分片数据的最后一个数据
		CLOSED = 0X08,//关闭客户端连接
		PING = 0X09,//表示ping
		PONG = 0XA0,//表示pong
	};
	union PacketHead
	{
		struct {
			uint8_t OPCODE : 4;//用于表示消息接收类型，如果接收到未知的opcode，接收端必须关闭连接。长连接探活包就是这里标识的。
			/*
			OPCODE定义的范围：
	　　    0x0表示一个延续帧。当Opcode为0时，表示本次数据传输采用了数据分片，当前收到的数据帧为其中一个数据分片。
	　　    0x1表示文本数据帧
	　　    0x2表示二进制数据帧
	　　    0x3-7暂时无定义，为以后的非控制帧保留
	　　    0x8表示连接关闭
	　　    0x9表示ping
	　　    0xA表示pong
	　　    0xB-F暂时无定义，为以后的控制帧保留
			*/
			uint8_t RSV3 : 1;//暂无定义
			uint8_t RSV2 : 1;//暂无定义
			uint8_t RSV1 : 1;//暂无定义
			uint8_t FIN : 1;//用于描述消息是否结束，如果为1则该消息为消息尾部,如果为零则还有后续数据包.
			uint8_t PAYLOAD : 7;//取值0-125，该位就为真实数据长度，后面全部为数据；取值126，则最后的2个字节为数据包的实际长度；取值为127，则最后8个字节表达为数据包实际长度。
			uint8_t MASK : 1;//用于标识PayloadData是否经过掩码处理，客户端发出的数据帧需要进行掩码处理，所以此位是1，数据需要解码。当是服务端发给客户端的代码时，此位为0，数据不需解码。
		}buf;
		uint8_t pack[2];
		//	struct{
		//		int64 PLEN:64;用于描述数据包长度.
		//	}payload;
		//	uint8_t PLpack[8];
		//	uint8_t Mpack[4];

	};
	union PacketFrame
	{
		struct {
			PacketHead head;            // 基础头部
			union {
				struct {
					uint8_t PAYLOADLEN[8];  // 扩展载荷长度
					uint8_t MASKS[4];             // 掩码键
				}buf;
				uint8_t data[12];       // 整体数据访问
			}pack;
		} frame;
		uint8_t raw[14];  // 整个帧头的原始字节表示 (2 + 8 + 4)
	};
	struct NoSioData
	{
		std::string _event;
		AssetLib::BinaryData _data;
		NoSioData() {
		}
		NoSioData(const char* ev, const AssetLib::BinaryData& d) {
			_event = ev;
			_data = d;
		}
		NoSioData(const char* ev, AssetLib::BinaryData&& d)
		{
			_event = ev;
			_data = std::move(d);
		}
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s% _event% _data;
		}
	};
	enum class MsgCode {
		SOCKET_ERR_EACCES = 13, // 权限被拒绝，例如尝试广播但未设置广播权限。
		SOCKET_ERR_EADDRINUSE = 98, // 地址已在使用，试图绑定的端口已被其他套接字占用。
		SOCKET_ERR_EADDRNOTAVAIL = 99, //	地址不可用，尝试绑定到一个本地不存在的地址。
		SOCKET_ERR_EAFNOSUPPORT = 97, // 地址族不支持，指定的协议族（如AF_INET）未在系统中实现。
		SOCKET_ERR_EAGAIN = 11,
		SOCKET_ERR_EWOULDBLOCK =
		11, // 资源暂时不可用。在非阻塞套接字上，操作（如recv, send, accept）不能立即完成。
		// 这是非阻塞操作的正常情况，可稍后重试或使用select/poll等I/O多路复用机制等待。
		SOCKET_ERR_EALREADY = 114, // 操作已在进行中，在非阻塞套接字上尝试连接时，之前的连接请求还未完成。
		// 等待前一个连接尝试完成。
		SOCKET_ERR_EBADF = 9, // 无效的文件描述符，套接字参数不是有效的文件描述符。
		SOCKET_ERR_ECONNABORTED = 103, // 软件引起的连接中止，通常发生在连接建立后、accept完成前，对方发送了RST复位连接。
		// 服务器通常可忽略此错误并重新调用accept。
		SOCKET_ERR_ECONNREFUSED = 111, //	连接被拒绝，目标主机在指定端口没有进程等待连接。
		//检查目标地址和端口，确认服务是否开启。
		SOCKET_ERR_ECONNRESET = 104, //	连接被对方重置，对方强制关闭了连接。
		//应用应关闭套接字，检查通信协议或对方进程状态。
		SOCKET_ERR_EDESTADDRREQ = 89, //	需要目标地址，在未连接的套接字上操作未指定目标地址。
		//为操作（如sendto）指定目标地址。
		SOCKET_ERR_EFAULT = 14, // 系统调用参数指向了无效的内存地址。	检查指针参数有效性。
		SOCKET_ERR_EHOSTUNREACH = 113, //	主机不可达，路由失败。	检查网络路由和主机地址。
		SOCKET_ERR_EINPROGRESS = 115, //	操作正在进行中，在非阻塞套接字上发起的连接尚未完成。
		//使用select或poll检查套接字是否可写，以判断连接是否成功。
		SOCKET_ERR_EINTR = 4, //	系统调用被信号中断。	通常重启被中断的系统调用即可。
		SOCKET_ERR_EINVAL = 22, //	无效的参数，或套接字状态与操作不匹配（如对未处于监听状态的套接字调用accept）。
		//检查参数和套接字状态。
		SOCKET_ERR_EISCONN =
		106, //	传输端点已连接，在已连接的套接字上再次尝试连接。	检查代码逻辑，避免重复连接。
		SOCKET_ERR_ELOOP = 40,  //	遇到符号链接循环。	检查路径中的符号链接。
		SOCKET_ERR_EMFILE = 24, //	进程打开文件数达到上限，无法创建新的套接字。
		//调整进程文件描述符限制，或检查代码是否存在描述符泄漏。
		SOCKET_ERR_EMSGSIZE = 90,     //	消息大小超出限制。	减小发送数据块大小。
		SOCKET_ERR_ENETDOWN = 100,    //	网络接口已关闭。	检查网络连接。
		SOCKET_ERR_ENETUNREACH = 101, //	网络不可达。	检查网络路由。
		SOCKET_ERR_ENFILE = 23, //	系统打开文件总数达到上限。	需要系统级检查和管理。
		SOCKET_ERR_ENOBUFS = 105, // 内存不足，系统缺乏足够的缓冲区资源。
		SOCKET_ERR_ENOMEM = 12,   // 内存不足，系统缺乏足够的缓冲区资源。
		SOCKET_ERR_ENODEV =
		19, // 找不到设备，例如试图在不存在网络设备上绑定地址。	检查网络设备状态。
		SOCKET_ERR_ENOENT = 2, // 文件或目录不存在，可能路径问题影响Unix域套接字。	检查路径。
		SOCKET_ERR_ENOPROTOOPT =
		92, // 协议层不认识此选项，level或optname参数错误。	检查setsockopt或getsockopt参数。
		SOCKET_ERR_ENOTCONN = 107, //	传输端点未连接，在未连接的套接字上调用send或recv等需要连接的操作。
		//确保套接字已成功连接。
		SOCKET_ERR_ENOTSOCK = 88,  //	描述符不是套接字。	检查文件描述符。
		SOCKET_ERR_EOPNOTSUPP =
		95, //	操作不支持，例如对数据报套接字调用listen。	检查操作与套接字类型是否匹配。
		SOCKET_ERR_EPERM = 1, // 操作不被允许，可能被防火墙规则阻止。	检查防火墙设置。
		SOCKET_ERR_EPIPE =
		32, //	管道破裂，向已关闭写的连接写入数据，会触发SIGPIPE信号（默认终止进程）。
		//忽略SIGPIPE信号，并通过send的MSG_NOSIGNAL标志避免信号产生，转而通过返回值检测错误。
		SOCKET_ERR_EPROTO = 71, // 协议错误。	检查协议实现。
		SOCKET_ERR_EPROTONOSUPPORT = 93, // 协议类型不支持，或域内无此协议。	检查socket函数参数。
		SOCKET_ERR_EROFS = 30, //	文件系统只读，可能影响Unix域套接字创建。	检查文件系统权限。
		SOCKET_ERR_ESOCKTNOSUPPORT =
		94, // 套接字类型在该地址族中不支持。	检查地址族和套接字类型组合。
		SOCKET_ERR_ETIMEDOUT = 110, // 连接超时，对方主机崩溃或中间网络问题导致无响应
		SOCKET_ERR_CREATED = 0x2000, // socket已创建，无需再创
		SOCKET_ERR_FLAG = 0x2001,    // socket标志错
		SOCKET_ERR_DNS = 0x2002,     // socket域名或IP解析发生错
		// 自定义Epoll错误码
		EPOLL_ERR_CREATED = 0x2000,         // 创建epoll实例失败
		EPOLL_ERR_CTL_OPER_FAILED = 0x2001, // epoll_ctl操作失败
		EPOLL_ERR_WAIT_FAILED = 0x2002,     // epoll_wait失败
		EPOLL_ERR_INVALID_EVENT = 0x2003,   // 无效的事件类型
	};
	void PrintPut(const String& str);
	Value ParseValue(const Value& v);
	const char* MessageDesc(MsgType type, MsgCode code, int value = 0);
	const char* MessageDesc(MsgType type, int code, int value = 0);
}; // namespace NetLibrary
#endif
