#include "../inc/include.h"
sockaddr_in& UserInfo::GetclientAddr()
{
	Mutex::Lock __(lock);
	return clientAddr;
}
const char* UserInfo::GetclientIP()
{
	Mutex::Lock __(lock);
	return inet_ntoa(clientAddr.sin_addr);
}
void NetLibrary::UserInfo::SetclientAddr(const sockaddr_in& addr)
{
	Mutex::Lock __(lock);
	clientAddr = addr;
}
void UserInfo::SetFd(SOCKET s)
{
	Mutex::Lock __(lock);
	c_Socket = s;
}
SOCKET UserInfo::GetFd()
{
	Mutex::Lock __(lock);
	return c_Socket;
}
void UserInfo::Release()
{
	Mutex::Lock __(lock);
	if(ssl)
	{
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
	ssl.Release();
	if (c_Socket)
#ifdef PLATFORM_WIN32
	{
		shutdown(c_Socket, SD_BOTH);
		CancelIoEx((HANDLE)c_Socket, NULL);
		closesocket(c_Socket);
	}
#else
	{
		shutdown(c_Socket, SHUT_RDWR);
		close(c_Socket);
	}
#endif
	c_Socket = INVALID_SOCKET;
	pingpong = 0;
	ptype = ProtocolType::DECONNECT;
	clientAddr = { 0 };
}
int UserInfo::GetPingPong()
{
	Mutex::Lock __(lock);
	return pingpong;
}
void UserInfo::SetPingPong(int mil)
{
	Mutex::Lock __(lock);
	pingpong = mil;
}
Uuid UserInfo::Getsid()
{
	Mutex::Lock __(lock);
	return sid;
}

bool UserInfo::CreatSSLuser(NetSSLContext& sslctx)
{
	Mutex::Lock __(lock);
	if (!ssl.CreateSSLSession(sslctx)) {
		NetException::PrintMsg("Error:Failed to create SSL object for fd: %d", c_Socket);
		return false;
	}
	return true;
}
	
void UserInfo::SetProtocol(ProtocolType type)
{
	Mutex::Lock __(lock);
	ptype = type;
}
void UserInfo::Setsid(Uuid session)
{
	Mutex::Lock __(lock);
	sid = session;
}
ProtocolType UserInfo::GetProtocol()
{
	Mutex::Lock __(lock);
	return ptype;
}
UserInfo::UserInfo(size_t k)
{
	key = k;
}