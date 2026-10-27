#pragma once

#include <mutex>

namespace AccountServer {

/// <summary>
/// 登录码解码后的载荷。
/// </summary>
struct AdminLoginCodePayload
{
	int     ver = 1;
	String  username_lc;
	String  challenge_id;
	int64   ts = 0;
	uint64  nonce = 0;
};

/// <summary>
/// 防重放存储接口。
/// </summary>
class IAdminNonceStore
{
public:
	virtual ~IAdminNonceStore() {}
	/// <summary>
	/// 尝试消费一次 nonce。
	/// 返回 true 代表首次使用；false 代表重复或无效。
	/// </summary>
	virtual bool TryUse(const String& scope_key, int64 expire_at) = 0;
};

/// <summary>
/// 进程内内存版防重放存储（可替换为 Redis 实现）。
/// </summary>
class AdminMemoryNonceStore : public IAdminNonceStore
{
public:
	bool TryUse(const String& scope_key, int64 expire_at) override;

private:
	std::mutex lock_;
	VectorMap<String, int64> used_;
	void CleanupLocked(int64 now);
};

/// <summary>
/// 管理后台登录码服务。
/// 登录码格式：Base64(payload_raw) + "." + Base64(HMAC_SHA256(payload_raw, device_secret))
/// </summary>
class AdminLoginCodeService
{
public:
	AdminLoginCodeService();

	/// <summary>
	/// 设置登录码允许的时间窗口（秒），默认 60 秒。
	/// </summary>
	void SetTimeWindowSec(int64 sec);

	/// <summary>
	/// 设置防重放存储；不设置时使用内置内存实现。
	/// </summary>
	void SetNonceStore(IAdminNonceStore* store);

	/// <summary>
	/// 生成登录码（通常由 EXE 使用）。
	/// </summary>
	bool GenerateLoginCode(const String& username,
						  const String& challenge_id,
						  const String& device_secret,
						  String& out_code,
						  String* error = nullptr) const;

	/// <summary>
	/// 校验登录码（通常由 AccountServer 使用）。
	/// expected_username / expected_challenge_id 可为空；为空表示不做该项匹配。
	/// </summary>
	bool VerifyLoginCode(const String& login_code,
						const String& device_secret,
						const String& expected_username,
						const String& expected_challenge_id,
						AdminLoginCodePayload* out_payload = nullptr,
						String* error = nullptr);

	static int64 NowSec();

private:
	int64 time_window_sec_ = 60;// 登录码允许的时间窗口（秒）
	IAdminNonceStore* nonce_store_ = nullptr;// 防重放存储；不设置时使用内置内存实现
	AdminMemoryNonceStore default_store_;// 内置内存实现

	static String NormalizeUsername(const String& s);
	static String BuildScopeKey(const String& username_lc, const String& challenge_id, uint64 nonce);

	static void PutU16(String& out, uint16 v);
	static void PutU64(String& out, uint64 v);
	static bool GetU16(const String& in, int& off, uint16& v);
	static bool GetU64(const String& in, int& off, uint64& v);

	static bool RandomNonce(uint64& out_nonce);
	static bool HmacSha256(const String& key, const String& data, String& out32);
	static bool ConstantTimeEqual(const String& a, const String& b);

	static String BuildPayloadRaw(const String& username_lc,
								 const String& challenge_id,
								 int64 ts,
								 uint64 nonce);
	static bool ParsePayloadRaw(const String& raw, AdminLoginCodePayload& out);
};

}
