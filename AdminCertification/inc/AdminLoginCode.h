#pragma once

#include <mutex>

namespace AdminCertification {

struct AdminLoginCodePayload
{
	int     ver = 1;
	String  username_lc;
	String  challenge_id;
	int64   ts = 0;
	uint64  nonce = 0;
};

class IAdminNonceStore
{
public:
	virtual ~IAdminNonceStore() {}
	virtual bool TryUse(const String& scope_key, int64 expire_at) = 0;
};

class AdminMemoryNonceStore : public IAdminNonceStore
{
public:
	bool TryUse(const String& scope_key, int64 expire_at) override;

private:
	std::mutex lock_;
	VectorMap<String, int64> used_;
	void CleanupLocked(int64 now);
};

class AdminLoginCodeService
{
public:
	AdminLoginCodeService();

	void SetTimeWindowSec(int64 sec);
	void SetNonceStore(IAdminNonceStore* store);

	bool GenerateLoginCode(const String& username,
		const String& challenge_id,
		const String& device_secret,
		String& out_code,
		String* error = nullptr) const;

	bool VerifyLoginCode(const String& login_code,
		const String& device_secret,
		const String& expected_username,
		const String& expected_challenge_id,
		AdminLoginCodePayload* out_payload = nullptr,
		String* error = nullptr);

	static int64 NowSec();

private:
	int64 time_window_sec_ = 60;
	IAdminNonceStore* nonce_store_ = nullptr;
	AdminMemoryNonceStore default_store_;

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
