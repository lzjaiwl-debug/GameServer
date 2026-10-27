#include "../AccountServer.h"

void AccountServer::listenBase::StartNetServer(const NetLibrary::njson& config)
{
	try {
		net_interface_ = NetLibrary::CreateNetLibrary(true);
		net_interface_->On(EVT_BASE::REQUEST, this, &listenBase::ReceiveMessages);
		net_interface_->On(NetLibrary::SYSEVENT::CONNECT, this, &listenBase::HandleClientConnect);
		net_interface_->On(NetLibrary::SYSEVENT::DISCONNECT, this, &listenBase::HandleClientDisconnect);
		net_interface_->On(NetLibrary::SYSEVENT::SYSERR, this, &listenBase::HandleSystemError);
		net_interface_->On(NetLibrary::SYSEVENT::CONNECTERR, this, &listenBase::HandleSystemError);
		net_interface_->On(NetLibrary::SYSEVENT::DATAERR, this, &listenBase::HandleSystemError);
		net_interface_->On(NetLibrary::SYSEVENT::SYSLOG, this, &listenBase::HandleSystemLog);
		net_interface_->StartNetSver(config.dump(4).c_str());
	}
	catch (const Upp::Exc& ex) {
		sharedFeatures::PrintMsg("Failed to start network server: %s", ex);
	}
	catch (const std::exception& ex) {
		sharedFeatures::PrintMsg("Failed to start network server: %s", ex.what());
	}
	catch (...) {
		sharedFeatures::PrintMsg("Failed to start network server: Unknown error");	
	}
}

void AccountServer::listenBase::StopNetServer()
{
	queue_.Stop();
	client_map_.Clear();
	timeAsync = nullptr;
	if(net_interface_)
	{
		net_interface_->ShutDownNetSver();
		delete net_interface_;
		net_interface_ = nullptr;
	}
}

void AccountServer::listenBase::HandleClientConnect(const AssetLib::BinaryData& data)
{
	NetLibrary::Clientinfo info;
	AssetLib::SerializerFactory::Deserialize(info, data);
	client_map_.InsertDerived(ScanUuid(info.sid.c_str()), MakeShared<USER_INFO>(info));
	sharedFeatures::PrintMsg("Client connected: sid=%s, ip=%s, isSocketIO=%s", ~ScanUuid(info.sid.c_str()).ToStringWithDashes(), info.ip.c_str(), info.isSocketIO ? "true" : "false");
}

void AccountServer::listenBase::HandleClientDisconnect(const AssetLib::BinaryData& data)
{
	Uuid sid = ScanUuid(data);
	SharedPtr<USER_INFO> userinfo = nullptr;
	{
		client_map_.TryGetCopy(sid, userinfo);
		client_map_.Erase(sid);
	}
	sharedFeatures::PrintMsg("Client disconnected: sid=%s, ip=%s, isSocketIO=%s", ~sid.ToStringWithDashes(), userinfo ? userinfo->clientinfo.ip.c_str() : "unknown", userinfo ? (userinfo->clientinfo.isSocketIO ? "true" : "false") : "unknown");
}

void AccountServer::listenBase::HandleSystemError(const AssetLib::BinaryData& data)
{
	String errorMsg = data;
	sharedFeatures::PrintMsg("System error: %s", errorMsg);
}

void AccountServer::listenBase::HandleSystemLog(const AssetLib::BinaryData& data)
{
	String logMsg = data;
	sharedFeatures::PrintMsg("System log: %s", logMsg);
}