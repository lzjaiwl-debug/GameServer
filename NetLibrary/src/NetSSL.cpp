#include "../inc/include.h"

bool NetLibrary::NetSSLContext::InitSSLContext()
{
    if(*this != nullptr) {
        // Already initialized
        return true;
	}
	if(certificate.IsEmpty() || private_key.IsEmpty())
    {
		NetException::PrintMsg("InitSSLContext Function failed. Certificate or private key not loaded.");
        return false;
    }
	SSL_METHOD* method = nullptr;
    if (isserver) {
        method = (SSL_METHOD*)TLS_server_method();
    }
    else {
        method = (SSL_METHOD*)TLS_client_method();
    }
	if (!Create(method)) {
		NetException::PrintMsg("InitSSLContext Function failed. Failed to create SSL context.");
		return false;
	}
	// 设置默认的密码套件列表
    // 注意：OpenSSL cipher list 的分隔符是 ':' 或空格，'+' 非法会导致整个串解析失败
    // TLS 1.3 的 ciphersuites 不走这里（用 SSL_CTX_set_ciphersuites），会自动使用默认套件的
    if (!CipherList("HIGH:!aNULL:!eNULL:!MD5:!RC4:!3DES")) {
		NetException::PrintMsg("InitSSLContext Function failed. Failed to set SSL cipher list.");
		this->Clear();
		return false;
	}
	bool isasn1 = false;
	String certExt = GetFileExt(certificate);
	String keyExt = GetFileExt(private_key);
	if (certExt == "der" || keyExt == "der")//证书是二进制格式
		isasn1 = true;
	// 配置证书和私钥（这里假设有默认路径，可以根据需要修改）
	if (!UseCertificate(::LoadFile(certificate), ::LoadFile(private_key),isasn1)) {
		NetException::PrintMsg("InitSSLContext Function failed. Failed to use SSL certificate and private key.");
        this->Clear();
        return false;
	}
	// 配置CA证书以验证对等方
    // CA 证书只在「需要校验对端」时才有意义。
    // 服务端对外提供 TLS 服务时不该索要客户端证书，因此不需要加载 CA；
    // 硬编码 "ca.crt" 会在文件缺失时直接让初始化失败，进而被静默降级为明文。
    if (!ca_cert.IsEmpty()) {
        if (!FileExists(ca_cert)) {
            NetException::PrintMsg("InitSSLContext Function failed. CA file not found: %s", ~ca_cert);
            this->Clear();
            return false;
        }
        if (!UseCAcert(ca_cert, isasn1)) {
            NetException::PrintMsg("InitSSLContext Function failed. Failed to use CA certificate.");
            this->Clear();
            return false;
        }
    }
	// 启用对等方验证
    // U++ 的 VerifyPeer(true) 等价于 SSL_CTX_set_verify(SSL_VERIFY_PEER)。
    // 在服务端开启它 = 强制索要客户端证书，游戏客户端没有证书会直接握手失败。
    // 因此：服务端关闭，客户端开启。
    if (isserver)
        VerifyPeer(false);
    else
        VerifyPeer(true, 4);
    return true;
}

bool NetLibrary::NetSSLContext::LoadCertificate(const String& cert, const String& pkey)
{
	if(!FileExists(cert) || !FileExists(pkey)) {
		NetException::PrintMsg("LoadCertificate Function failed. No key file or incorrect key file name [.pem .crt .cer .der|.privatekey].");
		return false;
	}
	certificate = cert;
	private_key = pkey;
	return true;
}


bool NetLibrary::NetSSL::CreateSSLSession(NetSSLContext& ctx)
{
	Release();
	ssl = SSL_new((SSL_CTX*)ctx);
	if (ssl == nullptr) {
		NetException::PrintMsg("CreateSSLSession Function failed. SSL_new returned null.");
        return false;
	}
    // Select server/client state
    if (ctx.IsServer())
        SSL_set_accept_state(ssl);
    else
        SSL_set_connect_state(ssl);

    // Common recommended modes
    long mode = SSL_get_mode(ssl);
    mode |= SSL_MODE_ACCEPT_MOVING_WRITE_BUFFER;
    mode |= SSL_MODE_ENABLE_PARTIAL_WRITE;
    SSL_set_mode(ssl, mode);

    // Configure by SSLType
    switch (type) {
    case SSLType::SYNC:
    case SSLType::ASYNC:
        // Mount existing socket if provided; expect non-blocking I/O at OS level
        if (sock != INVALID_SOCKET) {
            if (SSL_set_fd(ssl, (int)sock) != 1) {
                NetException::PrintMsg("CreateSSLSession Function failed. SSL_set_fd error.");
                SSL_free(ssl);
                ssl = nullptr;
                return false;
            }
        }
        // SSL will return WANT_READ/WANT_WRITE; caller must use non-blocking socket
        break;
    case SSLType::MEMORY: {
        // Create memory BIO pair for application-driven I/O
        BIO* rbio = BIO_new(BIO_s_mem());
        BIO* wbio = BIO_new(BIO_s_mem());
        if (!rbio || !wbio) {
            if (rbio) BIO_free(rbio);
            if (wbio) BIO_free(wbio);
            NetException::PrintMsg("CreateSSLSession Function failed. BIO_new returned null.");
            SSL_free(ssl);
            ssl = nullptr;
            return false;
        }
        // Set BIOs to SSL - SSL takes ownership
        SSL_set_bio(ssl, rbio, wbio);
        break;
    }
    default:
        break;
    }
    return true;
}

int NetLibrary::NetSSL::SSLDoHandshake()
{
	if (ssl == nullptr) {
		NetException::PrintMsg("SSLDoHandshake Function failed. SSL session is null.");
		return -1;
	}
    if(is_handshake_done)
		return 1; // already done
    int ret = SSL_do_handshake(ssl);
    if (ret == 1) {
        is_handshake_done = true;
        NetException::PrintMsg("SSL handshake completed successfully.");
        String log;
        // 输出 SSL 信息
        NetException::PrintMsg("Protocol: %s.", SSL_get_version(ssl));
        NetException::PrintMsg("Cipher: %s.", SSL_get_cipher(ssl));
        // 显示证书信息
        X509* cert = SSL_get_peer_certificate(ssl);
        if (cert) {
            char* subject = X509_NAME_oneline(X509_get_subject_name(cert), NULL, 0);
            char* issuer = X509_NAME_oneline(X509_get_issuer_name(cert), NULL, 0);
            NetException::PrintMsg("Subject: %s.", (subject ? subject : "N/A"));
            NetException::PrintMsg("Issuer: %s.", (issuer ? issuer : "N/A"));
            OPENSSL_free(subject);
            OPENSSL_free(issuer);
            X509_free(cert);
        }
        return 1;
    }
    int err = SSL_get_error(ssl, ret);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE) {
        // For async/memory, caller should retry when I/O becomes possible
        return 0;
    }
    NetException::PrintMsg("SSLDoHandshake failed. err=%s", GetSSLErrorString(err));
    return -1;
}

int NetLibrary::NetSSL::SSLPending()
{
    if (!ssl) return -1;
	return SSL_pending(ssl);
}

int NetLibrary::NetSSL::BIOPending()
{
    if (!ssl) return -1;
    BIO* rbio = SSL_get_rbio(ssl);
    if (!rbio) {
        NetException::PrintMsg("BIOPending Function failed. SSL_get_rbio returned null.");
        return -1;
    }
	return BIO_pending(rbio);
}

String NetLibrary::NetSSL::GetSSLErrorString(int err)
{
	String out;
	switch (err) {
#define SSLERR(c) case c: out = #c; break;
		SSLERR(SSL_ERROR_NONE)
			SSLERR(SSL_ERROR_SSL)
			SSLERR(SSL_ERROR_WANT_READ)
			SSLERR(SSL_ERROR_WANT_WRITE)
			SSLERR(SSL_ERROR_WANT_X509_LOOKUP)
			SSLERR(SSL_ERROR_SYSCALL)
			SSLERR(SSL_ERROR_ZERO_RETURN)
			SSLERR(SSL_ERROR_WANT_CONNECT)
#ifdef PLATFORM_WIN32
			SSLERR(SSL_ERROR_WANT_ACCEPT)
#endif
	default: out = "unknown code"; break;
	}
    return out;
}

int NetLibrary::NetSSL::SSLRead(char* buf, int size)
{
    if (!ssl) return -1;
    int n = SSL_read(ssl, buf, size);
    if (n > 0) return n;
    int err = SSL_get_error(ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        return 0; // try again later (async/memory)
    NetException::PrintMsg("SSLRead failed. err=%s", GetSSLErrorString(err));
    return -1;
}

int NetLibrary::NetSSL::SSLWrite(const char* buf, int size)
{
    if (!ssl) return -1;
    int n = SSL_write(ssl, buf, size);
    if (n > 0) return n;
    int err = SSL_get_error(ssl, n);
    if (err == SSL_ERROR_WANT_READ || err == SSL_ERROR_WANT_WRITE)
        return 0; // try again later (async/memory)
    NetException::PrintMsg("SSLWrite failed. err=%s", GetSSLErrorString(err));
    return -1;
}

int NetLibrary::NetSSL::BIORead(char* buf, int size)
{
	if (!ssl) return -1;
	BIO* rbio = SSL_get_rbio(ssl);
	if (!rbio) {
		NetException::PrintMsg("BIORead Function failed. SSL_get_rbio returned null.");
		return -1;
	}
	int n = BIO_read(rbio, buf, size);
	if (n > 0) return n;
	if (BIO_should_retry(rbio))
		return 0; // try again later
	NetException::PrintMsg("BIORead failed.");
	return -1;
}

int NetLibrary::NetSSL::BIOWrite(const char* buf, int size)
{
    if (!ssl) return -1;
    BIO* wbio = SSL_get_wbio(ssl);
    if (!wbio) {
        NetException::PrintMsg("BIOWrite Function failed. SSL_get_wbio returned null.");
        return -1;
    }
    int n = BIO_write(wbio, buf, size);
    if (n > 0) return n;
    if (BIO_should_retry(wbio))
        return 0; // try again later
    NetException::PrintMsg("BIOWrite failed.");
	return -1;
}

void NetLibrary::NetSSL::Release()
{
    if (ssl) {
        SSL_free(ssl);
        ssl = nullptr;
    }
    is_handshake_done = false;
    sock = INVALID_SOCKET;
}
