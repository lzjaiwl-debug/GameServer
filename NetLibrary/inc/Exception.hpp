#ifndef _NetLibrary_Exception_h_
#define _NetLibrary_Exception_h_
namespace NetLibrary {
// 颜色和样式定义
    #define RESET "\033[0m"
    #define RED "\033[31m"
    #define GREEN "\033[32m"
    #define YELLOW "\033[33m"
    #define BLUE "\033[34m"
    #define MAGENTA "\033[35m"
    #define CYAN "\033[36m"
    #define WHITE "\033[37m"
    #define BOLD "\033[1m"   // 粗体或高亮
    #define UNDERLINE "\033[4m" // 下划线
enum class ExcType//抛出错误的类型
{
	SOCKETERR,//SOCKER系统级错误
	EPOLLERROR,//EPOLL系统级错误
	COMERR,//通讯类错误
	MEMERR,//缓存错误
	PROERR,//协议级错误
	SSLERR,//SSL级别错误
};
class NetException : public Exc
{
private:
	int ecode;
	int64 ememaddr;
	ExcType type;
public:
    template <typename T>
    static constexpr auto ToFormatArg(T&& v)
    {
        using D = std::decay_t<T>;
        if constexpr (std::is_arithmetic_v<D> && !std::is_same_v<D, int>)
            return static_cast<int>(v);
        else
            return std::forward<T>(v);
    }
	NetException(ExcType t,int code,int64 memaddr,const char* des):type(t),ecode(code),ememaddr(memaddr),Exc(Format("[%`]Error:%s.Code:%d, Address:%d.",GetSysTime(), des,code,memaddr)){
		ecode = code;
		ememaddr = memaddr;
#ifndef _DEBUG
		Cout() << RED << this << RESET;
#else
		RLOG(this);
#endif // _DEBUG
	};
	template <typename... Args>
    NetException(ExcType t, int code, int64 memaddr, const char* s, const Args&... args)
        : NetException(t, code, memaddr, ~Format(s, gather<Vector<Value>>(ToFormatArg(args)...)))
    {
	}
	ExcType getErrType()
	{
		return type;
	}
	template <typename... Args>
	static void PrintMsg(const char* s, const Args&... args)
	{
		// 构建时间戳
		String timestamp = Format("[%`]", GetSysTime());

		// 构建消息内容
		String content;
		try {
			if constexpr(sizeof...(args) > 0) {
                content = Format(s, gather<Vector<Value>>(ToFormatArg(args)...));
			}
			else {
				content = s;
			}
		}
		catch(...) {
			content = "Format error in message: " + String(s);
		}
		// 输出带有颜色的消息
#ifndef _DEBUG
		Cout() << BLUE << timestamp << content << RESET;
#else
		LOG(timestamp + content);
#endif // _DEBUG
		
	}
};
};
#endif
