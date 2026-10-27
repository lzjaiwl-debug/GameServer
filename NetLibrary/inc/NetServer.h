#ifndef _NetLibrary_ServerApp_h_
#define _NetLibrary_ServerApp_h_
namespace NetLibrary {
class NetServer : public TcpServer, public NetInterface
{
	typedef NetServer CLASSNAME;
private:
public:
	NetServer(){
		
	}
	virtual ~NetServer() override = default;
	virtual void On(EMIEVENT ev, AssetLib::EventHandler&& fun) override;//注册事件
	virtual void Emit(EMIEVENT ev, AssetLib::BinaryData data, bool isBinary = false) override;//发送事件
	virtual void StartNetSver(const char* host, int port, int unum, size_t memsize) override;
	virtual void StartNetSver(const char* conf) override;
	virtual void ShutDownNetSver() override;
	virtual void DisconnectClient(const char* sid) override;
};
};
#endif
