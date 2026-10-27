#pragma once
namespace NetLibrary {
	enum class SSLType {
		SYNC,//
		ASYNC,
		MEMORY,
	};
	class NetSSLContext : public SslContext {
		typedef NetSSLContext CLASSNAME;
	public:
		NetSSLContext() : isserver(true), SslContext() {}
		NetSSLContext(bool is) : isserver(is), SslContext() {}
		~NetSSLContext() {
			Clear();
		}
		bool InitSSLContext();
		bool LoadCertificate(const String& cert, const String& pkey);
		bool IsServer() const {
			return isserver;
		}
		void SetServer(bool is) {
			isserver = is;
		}
	private:
		bool isserver = false;
		String certificate = Nuller();
		String private_key = Nuller();
		String ca_cert = Nuller();
	};
	class NetSSL : public Moveable<NetSSL> {
		typedef NetSSL CLASSNAME;
	public:
		NetSSL() : ssl(nullptr), type(SSLType::MEMORY) {}
		NetSSL(SSLType t) : ssl(nullptr), type(t) {}
		~NetSSL() {
			Release();
		}
		bool CreateSSLSession(NetSSLContext& ctx);
		int SSLRead(char* buf, int size);
		int SSLWrite(const char* buf, int size);
		int BIORead(char* buf, int size);
		int BIOWrite(const char* buf, int size);
		int SSLDoHandshake();
		int SSLPending();
		int BIOPending();
		void Release();
		void SetSocket(SOCKET s) {
			sock = s;
		}
		void SetSSLType(SSLType t) {
			type = t;
		}
		SSL* GetSSL() {
			return ssl;
		}
		operator SSL*() {
			return ssl;
		}
		operator bool() const {
			return ssl != nullptr;
		}
	private:
		String GetSSLErrorString(int err);
	private:
		SSLType type;
		SSL* ssl;
		bool is_handshake_done = false;
		SOCKET sock = INVALID_SOCKET;
	};
};