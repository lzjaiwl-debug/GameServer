#include "../inc/include.h"

void NetLibrary::NetServer::On(EMIEVENT ev, AssetLib::EventHandler&& fun)
{
	assetEventBus.On(ev, fun);
}

void NetLibrary::NetServer::Emit(EMIEVENT ev, AssetLib::BinaryData data, bool isBinary)
{
	SockData sd;
	AssetLib::SerializerFactory::Deserialize(sd, data);
	Uuid uid = ::ScanUuid(sd.sid.c_str());
	this->SendEvent(uid, ev, pick(sd.data), sd.ackId, sd.isBinary);
}
void NetLibrary::NetServer::StartNetSver(const char* host, int port, int unum, size_t memsize)
{
	try {
		this->StartServer(host, port, unum, memsize);
	}
	catch (NetException& e) {
		switch (e.getErrType())
		{
		case ExcType::MEMERR:
		case ExcType::SSLERR:
		case ExcType::EPOLLERROR:
		case ExcType::SOCKETERR:
			assetEventBus.Emit(SYSEVENT::SYSERR, ~e);//代表系统错误的推送核心基础事件类型
			break;
		case ExcType::COMERR:
		case ExcType::PROERR:
			assetEventBus.Emit(SYSEVENT::CONNECTERR, ~e);//代表系统错误的推送核心基础事件类型
			break;

		default:
			break;
		}
	}
}

void NetLibrary::NetServer::StartNetSver(const char* conf)
{
	Value config = ParseJSON(conf);
	if (config.IsError())
	{
		assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to parse server configuration file.");
		return;
	}
	String listenaddr = config["listenaddr"];
	int listenport = config["listenport"] ? config["listenport"] : 8080;
	int concurrency = config["concurrency"] ? config["concurrency"] : 10;
	size_t cachesize = config["cachesize"] ? config["cachesize"].To<double>() * 1024 * 1024 : 1024 * 1024 * concurrency; // 默认缓存大小为 1MB * concurrency
		//<double>() * concurrency : 1024 * 1024 * concurrency;
	if (config["sslconf"])
	{
		const Value& sslconfig = config["sslconf"];
		if (sslconfig.IsError())
		{
			assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to parse SSL configuration file.");
			return;
		}
		String certfile = sslconfig["certfile"];
		String keyfile = sslconfig["keyfile"];
		if (!InitSSL(certfile, keyfile))
		{
			assetEventBus.Emit(SYSEVENT::SYSERR, "Failed to initialize SSL context.");
			return;
		}
	}
	this->StartNetSver(listenaddr, listenport, concurrency, cachesize);
}

void NetLibrary::NetServer::ShutDownNetSver()
{
	this->ShutDown();
	assetEventBus.ClearAll();	
}

void NetLibrary::NetServer::DisconnectClient(const char* sid)
{
	if (!sid || !*sid)
		return;
	this->DelUser(ScanUuid(sid));
}
