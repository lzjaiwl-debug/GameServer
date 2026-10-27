#ifndef _NetLibrary_SocketFun_h_
#define _NetLibrary_SocketFun_h_
namespace NetLibrary {
class SocketFun{
	typedef SocketFun CLASSNAME;
private:
	SockType stype = SockType::NONE;
	AssetLib::MemoryPool mpool;
	AssetLib::ThreadWorkPool<void, One<SockPacket>> threadpool;
    VectorMap<SOCKET,One<SockPacket>> piecepacks;//碎片数据缓存
    VectorMap<SOCKET,One<SockPacket>> blockpacks;//分块数据缓存
    Mutex piecepacks_mtx;
	Mutex blockpacks_mtx;
	Mutex _memret_mtx;
	void GetMask(uint8_t* mask);
	void HandleBlockPack(PacketFrame* frame, One<SockPacket> pack);
	void HandlePiecePack(One<SockPacket>& pack);//处理碎片数据
	void HandleNoHandshake(One<SockPacket> pack);
	void HandleIsHandshake(One<SockPacket> pack);
	void HandleReadEvent(One<SockPacket> pack);
	void HandleSendEvent(One<SockPacket> pack);
	void HandleQueueEvent(One<SockPacket>& pack);
	void MemoryFreeRet(One<SockPacket>& pack, size_t size = 0);
	// Callback used by MemoryPool::whendeallocate (expects only size)
	void MemoryFreeSize(size_t size);
	PacketFrame HandleBuildHeader(One<SockPacket>& pack, OPCODE code , size_t dlen);
	void HandleBuildPack(One<SockPacket> pack, OPCODE code, char* d = NULL, int size = 0);
	void SubPacket(SOCKET fd, bool isHandshake, bool isBinary, const AssetLib::BinaryData& d, NetSSL* s = NULL);
public:
	SocketFun(){}
	SocketFun(SockType type) : stype(type){
#ifdef PLATFORM_POSIX
		threadpool.Create();
		threadpool.StartAll();
#endif // PLATFORM_POSIX
	}
protected:
	Event<EventType,One<SockPacket>&, Value> WhenEvent;
	Event<SOCKET,const char*> WhenClientClosed;
	One<SockPacket> CreatSockPacket(SOCKET fd, PacketType type, bool isHandshake, bool isBinary, NetSSL* s = NULL);
	One<SockPacket> CreatCopySockPacket(One<SockPacket>& pack);
	void SubmitWork(One<SockPacket> pack, bool isSignal = true, bool isHead = true);
	void SubmitWork(UserInfo* u, OPCODE code, AssetLib::BinaryData d = AssetLib::BinaryData());
	void SubmitWork(UserInfo* u, AssetLib::BinaryData d = AssetLib::BinaryData(), bool isBinary = false, bool issend = true);
	void SetMemorySize(size_t size);
	bool IsAnyMem(size_t size);
	void ShutDownFn();
	//ProtocolType ParseProtocol(SOCKET ufd);
};
}; // namespace NetLibrary
#endif
