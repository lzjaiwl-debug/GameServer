#ifndef _NetLibrary_userinfo_h_
#define _NetLibrary_userinfo_h_
namespace NetLibrary {
class UserInfo : public AssetLib::PoolParent {
private:
	int64 pingpong = 0;
	NetSSL ssl;//SSL 会话对象
	SOCKET c_Socket = INVALID_SOCKET; // 接收接入进来的用户SOCKET对象
	ProtocolType ptype = ProtocolType::DECONNECT; // 标志用户接入用户客户端协议是websocket还是标准socket
	Uuid sid;//记录用户的session
	struct sockaddr_in clientAddr = {0}; // 用于储存接入用户的IP地址信息
	Mutex lock;
	BIO* read_bio;      // 用于从网络接收加密数据
	BIO* write_bio;     // 用于向网络发送加密数据
public:
	UserInfo(){}
	UserInfo(size_t k);
	~UserInfo(){
		Release();
	}
	int GetPingPong();
	void SetPingPong(int mil);
	sockaddr_in& GetclientAddr();
	void SetclientAddr(const sockaddr_in& addr);
	const char* GetclientIP();
	void SetFd(SOCKET s);
	SOCKET GetFd();
	void SetProtocol(ProtocolType type);
	ProtocolType GetProtocol();
	void Setsid(Uuid session);
	Uuid Getsid();
	bool CreatSSLuser(NetSSLContext& sslctx);
	NetSSL& GetSSL() {
		return ssl;
	};
	NetSSL* GetSSL_Ptr() {
		return ssl ? &ssl : nullptr;
	};
	virtual void Release();
};
}; // namespace NetLibrary
#endif