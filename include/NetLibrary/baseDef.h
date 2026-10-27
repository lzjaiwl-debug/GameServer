#ifndef _NetLibrary_baseDef_h_
#define _NetLibrary_baseDef_h_
// 兜底声明 SOCKET：避免依赖包含顺序，也避免与外部 _WINSOCKAPI_ / Windows.h 冲突
#ifdef _WIN32
	// WinSock2.h 中的官方定义是 typedef UINT_PTR SOCKET;
	// 此处不强制包含 winsock2.h（外部已定义 _WINSOCKAPI_ 会阻止它），仅在未定义时兜底
#ifndef SOCKET
#include <BaseTsd.h> // 提供 UINT_PTR
typedef UINT_PTR SOCKET;
#endif
#else
#ifndef SOCKET
typedef int SOCKET;
#endif
#endif
namespace NetLibrary {
	typedef const char* EMIEVENT;// 事件名称，字符串类型
	using njson = nlohmann::json;
	//系统级预定级事件，包括错误，断联，用户握手连接成功等等
	struct SYSEVENT {
		inline static const char* SYSLOG = "sys_log";//系统日志输出
		inline static const char* SYSERR = "sys_error";//系统级错误提示
		inline static const char* CONNECT = "connect";//用户握手且连接成功
		inline static const char* DISCONNECT = "disconnect";//用户断开连接
		inline static const char* CONNECTERR = "connect_error";//连接错误事件
		inline static const char* DATAERR = "data_error";//数据错误事件
	};
	struct Clientinfo
	{
		std::string sid;
		std::string ip;
		bool isSocketIO = false;
		Clientinfo() = default;
		Clientinfo(const char* s, const char* i, bool isSio)
			: sid(s ? s : ""), ip(i ? i : ""), isSocketIO(isSio) {
		}
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s% ip% sid% isSocketIO;
		}
	};
	struct SockData {
		bool isBinary;
		int ackId = -1;
		std::string sid;
		AssetLib::BinaryData data;
		SockData() = default;
		SockData(bool is, int ack, const char* s, AssetLib::BinaryData d)
			: isBinary(is), ackId(ack), sid(s ? s : ""), data(d) {
		}
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s % isBinary % ackId % sid % data;
		}

		bool operator==(const SockData& other) const
		{
			return false;
		}
	};
	struct HttpData {
		bool isfile;//是否是文件
		Httplib::StatusCode code;
		std::string sid; // 扩展代码字段
		std::string codeex;
		std::string data;
		std::string bodyex;
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s% isfile% code% sid% codeex% data% bodyex;
		}
	};
	/*
	struct Data
	{
		int len = 0;
		char* data = NULL;
		Data(){};
		Data(const Data& d){
			AllocateMemory(d.len,d.data);
		};
		Data(Data&& other){
			len = other.len;
			data = other.data;
			other.data = NULL;
		};
		Data(char* d,int l):len(l),data(d){};
		Data(const char* str){
			if (str) {
				AllocateMemory(::strlen(str), str);
			} else {
				// 处理nullptr情况
				len = 0;
				data = nullptr;
			}
		};
		void Allocate(int size)
		{
			if(data!=NULL)
				Free();
			len = size;
			data = (char*)::malloc(size);
			::memset(data,0x00,size);
		}
		void AllocateMemory(int size, const void* con)
		{
			Allocate(size);
			::memcpy(data,con,size);
		}
		void CopyData(int offset, const void* con,int size)
		{
			memcpy(data + offset,con,size);
		}
		bool isEmpty()
		{
			return data == NULL;
		}
		Data& operator = (const Data& d)//深度拷贝
		{
			AllocateMemory(d.len,d.data);
			return *this;
		}
		Data& operator=(Data&& other) // 移交所有权
		{
			if(this != &other && isEmpty()) {
				len = other.len;
				data = other.data;
				other.data = NULL;
			}
			return *this;
		}
		Data& operator = (const char* str)//深度拷贝
		{
			AllocateMemory(::strlen(str),str);
			return *this;
		}
		operator const char* ()
		{
			return std::string(data,len).c_str();
		}
		virtual void Free()
		{
			if(data!=NULL)
				::free(data);
			data = NULL;
			len = 0;
		}
		virtual ~Data()
		{
			Free();
		}
	};
	class InteData
	{
	private:
		std::vector<Data> list;
		bool IsSerialize = false;
		char* ptr = NULL;
		char* begin = NULL;
		char* end = NULL;

		void copy(void* d, int len)
		{
			if (((int64)end - (int64)ptr) < len)
				throw("反序列化发生错误，序列化数据不合法");
			memcpy(d, ptr, len);
			if (((int64)ptr + len) < (int64)end)
				ptr += len;
			else
				ptr = begin;
		}
		void Fill(Data& t)
		{
			copy(&t.len, sizeof(t.len));
			if (t.len < 0)
				throw("反序列化发生错误，序列化数据不合法");
			t.Allocate(t.len);
			copy(t.data, t.len);
		}
		template <class T>
		void Fill(T& t)
		{
			copy(&t, sizeof(T));
		}
		void AddData(Data& t)
		{
			Data& d = list.emplace_back();
			d.Allocate(t.len + sizeof(t.len));
			d.CopyData(0, &t.len, sizeof(t.len));
			d.CopyData(sizeof(t.len), t.data, t.len);
			//d.len = t.len + sizeof(t.len);
			//d.data = (char*)::malloc(d.len);
			//memcpy(d.data,&t.len,sizeof(t.len));
			//memcpy(d.data+sizeof(t.len),t.data,t.len);
		}
		template <class T>
		void AddData(const T& t)
		{
			Data& d = list.emplace_back();
			d.Allocate(sizeof(t));
			d.CopyData(0, &t, sizeof(t));
			//d.len = sizeof(T);
			//d.data = (char*)::malloc(d.len);
			//memcpy(d.data,&t,d.len);
		}

		template <class T>
		void Serialize(T& t)
		{
			t.Serialize(*this);
		}
	public:
		InteData() { };
		template <class T, void(T::*)(InteData&) = &T::Serialize>
		InteData(T& t, const Data& d) {
			ptr = begin = d.data; end = d.data + d.len;
			IsSerialize = false;
			Serialize(t);
		};
		template <class T, void(T::*)(InteData&) = &T::Serialize>
		InteData(T& t) {
			IsSerialize = true;
			Serialize(t);
		};
		template <class T>
		InteData& operator%(T& t)
		{
			if (IsSerialize)
				AddData(t);
			else
				Fill(t);
			return *this;
		};
		template <class T, void(T::*)(InteData&) = &T::Serialize>
		InteData& operator%(T&& t)
		{
			Serialize(t);
			return *this;
		};
		InteData& operator>>(Data& d)
		{
			for (auto& a : list)
				d.len += a.len;
			d.Allocate(d.len);
			for (auto& a : list)
			{
				memcpy(d.data, a.data, a.len);
				d.data += a.len;
			}
			d.data -= d.len;
			return *this;
		};
		~InteData() {
			list.clear();
		}
		template <class T>
		static void Deserialization(T& t, const Data& d)
		{
			InteData dz(t, d);
		}
		template <class T>
		static void Serialize(T& t, Data& d)
		{
			InteData dz(t);
			dz >> d;
		}
		template <class T>
		static Data&& SerializeRef(T& t)
		{
			Data d;
			InteData dz(t);
			dz >> d;
			return std::move(d);
		}
	};
	*/
};
#endif
