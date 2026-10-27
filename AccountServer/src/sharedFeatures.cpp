#include "../AccountServer.h"
bool AccountServer::sharedFeatures::IsValidSvrconf(const NetLibrary::njson& svrconf)
{
	if (svrconf.is_null())
		return false;
	bool hasListenAddr = svrconf.contains("listenaddr") && svrconf["listenaddr"].is_string();
	bool hasListenPort = svrconf.contains("listenport") && svrconf["listenport"].is_number_integer();
	bool hasConcurrency = svrconf.contains("concurrency") && svrconf["concurrency"].is_number_integer();
	bool hasCacheSize = svrconf.contains("cachesize") && svrconf["cachesize"].is_number();
	bool hasSslConf = svrconf.contains("sslconf") && svrconf["sslconf"].is_object();
	bool hasSslcertfile = hasSslConf && svrconf["sslconf"].contains("certfile") && svrconf["sslconf"]["certfile"].is_string();
	bool hasSslkeyfile = hasSslConf && svrconf["sslconf"].contains("keyfile") && svrconf["sslconf"]["keyfile"].is_string();
	return hasListenAddr && hasListenPort && hasConcurrency && hasCacheSize && hasSslConf && hasSslcertfile && hasSslkeyfile;
}

bool AccountServer::sharedFeatures::IsValidMysqlconf(const NetLibrary::njson& mysqlconf)
{
	if (mysqlconf.is_null())
		return false;
	bool hasHost = mysqlconf.contains("host") && mysqlconf["host"].is_string();
	bool hasPort = mysqlconf.contains("port") && mysqlconf["port"].is_number_integer();
	bool hasUser = mysqlconf.contains("user") && mysqlconf["user"].is_string();
	bool hasPassword = mysqlconf.contains("password") && mysqlconf["password"].is_string();
	bool hasDbname = mysqlconf.contains("dbname") && mysqlconf["dbname"].is_string();

	return hasHost && hasPort && hasUser && hasPassword && hasDbname;
}

bool AccountServer::sharedFeatures::IsValidUsername(String s)
{
	if (s.GetLength() < 3 || s.GetLength() > 20) return false;
	String allowedChars = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_";
	for (auto c : s)
	{
		if (!allowedChars.Find(c))
			return false;
	}
	return true;
}

bool AccountServer::sharedFeatures::IsValidCredType(uint8_t t)
{
	return t == 1 || t == 2 || t == 3 || t == 10 || t == 11 || t == 12;
}

String AccountServer::sharedFeatures::GenerateAdminToken()
{
	unsigned char raw[32];
	if (RAND_bytes(raw, (int)sizeof(raw)) != 1)
		return String();
	String s((const char*)raw, (int)sizeof(raw));
	String b64 = Upp::Base64Encode(s);
	b64.Replace("+", "-");
	b64.Replace("/", "_");
	b64.Replace("=", "");
	return b64;
}

String AccountServer::sharedFeatures::GenerateRandomString(size_t length)
{
	static const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
	String result;
	result.Reserve(length);
	for (size_t i = 0; i < length; ++i)
	{
		unsigned char randomByte;
		if (RAND_bytes(&randomByte, 1) != 1)
			return String(); // 生成失败
		result.Cat(charset[randomByte % (sizeof(charset) - 1)]);
	}
	return result;
}


