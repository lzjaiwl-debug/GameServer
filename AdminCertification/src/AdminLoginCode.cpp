#include "../AdminCertification.h"


namespace AdminCertification {

int64 AdminLoginCodeService::NowSec()
{
	return Upp::GetUTCSeconds(Upp::GetSysTime());
}

bool AdminMemoryNonceStore::TryUse(const String& scope_key, int64 expire_at)
{
	if (scope_key.IsEmpty())
		return false;
	int64 now = AdminLoginCodeService::NowSec();
	std::lock_guard<std::mutex> lk(lock_);
	CleanupLocked(now);
	int idx = used_.Find(scope_key);
	if (idx >= 0 && used_[idx] > now)
		return false;
	used_.GetAdd(scope_key) = expire_at;
	return true;
}

void AdminMemoryNonceStore::CleanupLocked(int64 now)
{
	for (int i = used_.GetCount() - 1; i >= 0; --i) {
		if (used_[i] <= now)
			used_.Remove(i);
	}
}

AdminLoginCodeService::AdminLoginCodeService()
{
	nonce_store_ = &default_store_;
}

void AdminLoginCodeService::SetTimeWindowSec(int64 sec)
{
	time_window_sec_ = sec > 0 ? sec : 60;
}

void AdminLoginCodeService::SetNonceStore(IAdminNonceStore* store)
{
	nonce_store_ = store ? store : &default_store_;
}

String AdminLoginCodeService::NormalizeUsername(const String& s)
{
	return Upp::ToLower(Upp::TrimBoth(s));
}

String AdminLoginCodeService::BuildScopeKey(const String& username_lc, const String& challenge_id, uint64 nonce)
{
	return username_lc + "|" + challenge_id + "|" + Format("%llu", (int)nonce);
}

void AdminLoginCodeService::PutU16(String& out, uint16 v)
{
	out.Cat((char)((v >> 8) & 0xFF));
	out.Cat((char)(v & 0xFF));
}

void AdminLoginCodeService::PutU64(String& out, uint64 v)
{
	for (int i = 7; i >= 0; i--)
		out.Cat((char)((v >> (i * 8)) & 0xFF));
}

bool AdminLoginCodeService::GetU16(const String& in, int& off, uint16& v)
{
	if (off + 2 > in.GetCount()) return false;
	const byte* p = (const byte*)in.Begin();
	v = (uint16)(((uint16)p[off] << 8) | (uint16)p[off + 1]);
	off += 2;
	return true;
}

bool AdminLoginCodeService::GetU64(const String& in, int& off, uint64& v)
{
	if (off + 8 > in.GetCount()) return false;
	const byte* p = (const byte*)in.Begin();
	v = 0;
	for (int i = 0; i < 8; i++)
		v = (v << 8) | (uint64)p[off + i];
	off += 8;
	return true;
}

bool AdminLoginCodeService::RandomNonce(uint64& out_nonce)
{
	unsigned char b[8];
	if (RAND_bytes(b, (int)sizeof(b)) != 1)
		return false;
	out_nonce = 0;
	for (int i = 0; i < 8; i++)
		out_nonce = (out_nonce << 8) | (uint64)b[i];
	return true;
}

bool AdminLoginCodeService::HmacSha256(const String& key, const String& data, String& out32)
{
	unsigned char mac[32];
	unsigned int maclen = 0;
	if (!HMAC(EVP_sha256(), key.Begin(), key.GetCount(),
		(const unsigned char*)data.Begin(), data.GetCount(), mac, &maclen))
		return false;
	if (maclen != 32)
		return false;
	out32 = String((const char*)mac, 32);
	return true;
}

bool AdminLoginCodeService::ConstantTimeEqual(const String& a, const String& b)
{
	if (a.GetCount() != b.GetCount())
		return false;
	return CRYPTO_memcmp(a.Begin(), b.Begin(), a.GetCount()) == 0;
}

String AdminLoginCodeService::BuildPayloadRaw(const String& username_lc,
	const String& challenge_id,
	int64 ts,
	uint64 nonce)
{
	String out;
	out.Cat((char)1);
	PutU16(out, (uint16)username_lc.GetCount());
	out.Cat(username_lc);
	PutU16(out, (uint16)challenge_id.GetCount());
	out.Cat(challenge_id);
	PutU64(out, (uint64)ts);
	PutU64(out, nonce);
	return out;
}

bool AdminLoginCodeService::ParsePayloadRaw(const String& raw, AdminLoginCodePayload& out)
{
	if (raw.GetCount() < 1)
		return false;
	int off = 0;
	out.ver = (unsigned char)raw[off++];
	if (out.ver != 1)
		return false;

	uint16 ulen = 0, clen = 0;
	uint64 ts = 0, nonce = 0;
	if (!GetU16(raw, off, ulen)) return false;
	if (off + ulen > raw.GetCount()) return false;
	out.username_lc = raw.Mid(off, ulen);
	off += ulen;
	if (!GetU16(raw, off, clen)) return false;
	if (off + clen > raw.GetCount()) return false;
	out.challenge_id = raw.Mid(off, clen);
	off += clen;
	if (!GetU64(raw, off, ts)) return false;
	if (!GetU64(raw, off, nonce)) return false;
	if (off != raw.GetCount()) return false;
	out.ts = (int64)ts;
	out.nonce = nonce;
	return true;
}

bool AdminLoginCodeService::GenerateLoginCode(const String& username,
	const String& challenge_id,
	const String& device_secret,
	String& out_code,
	String* error) const
{
	auto seterr = [&](const char* e) {
		if (error) *error = e;
	};
	out_code.Clear();
	String uname = NormalizeUsername(username);
	String challenge = Upp::TrimBoth(challenge_id);
	if (uname.IsEmpty()) { seterr("username is empty"); return false; }
	if (challenge.IsEmpty()) { seterr("challenge is empty"); return false; }
	if (device_secret.IsEmpty()) { seterr("device secret is empty"); return false; }
	if (uname.GetCount() > 65535 || challenge.GetCount() > 65535) { seterr("username or challenge too long"); return false; }

	uint64 nonce = 0;
	if (!RandomNonce(nonce)) { seterr("random nonce failed"); return false; }
	int64 ts = NowSec();
	String raw = BuildPayloadRaw(uname, challenge, ts, nonce);
	String sig;
	if (!HmacSha256(device_secret, raw, sig)) { seterr("hmac failed"); return false; }

	out_code = Upp::Base64Encode(raw) + "." + Upp::Base64Encode(sig);
	seterr("");
	return true;
}

bool AdminLoginCodeService::VerifyLoginCode(const String& login_code,
	const String& device_secret,
	const String& expected_username,
	const String& expected_challenge_id,
	AdminLoginCodePayload* out_payload,
	String* error)
{
	auto seterr = [&](const char* e) {
		if (error) *error = e;
	};
	if (login_code.IsEmpty()) { seterr("login code is empty"); return false; }
	if (device_secret.IsEmpty()) { seterr("device secret is empty"); return false; }

	int dot = login_code.Find('.');
	if (dot <= 0 || dot >= login_code.GetCount() - 1) {
		seterr("login code format invalid");
		return false;
	}

	String payload_b64 = login_code.Left(dot);
	String sig_b64 = login_code.Mid(dot + 1);
	String payload_raw = Upp::Base64Decode(payload_b64);
	String sig = Upp::Base64Decode(sig_b64);
	if (payload_raw.IsEmpty() || sig.IsEmpty()) {
		seterr("base64 decode failed");
		return false;
	}

	AdminLoginCodePayload p;
	if (!ParsePayloadRaw(payload_raw, p)) {
		seterr("payload parse failed");
		return false;
	}

	String calc;
	if (!HmacSha256(device_secret, payload_raw, calc)) {
		seterr("hmac failed");
		return false;
	}
	if (!ConstantTimeEqual(calc, sig)) {
		seterr("signature mismatch");
		return false;
	}

	String expect_user = NormalizeUsername(expected_username);
	String expect_challenge = Upp::TrimBoth(expected_challenge_id);
	if (!expect_user.IsEmpty() && p.username_lc != expect_user) {
		seterr("username mismatch");
		return false;
	}
	if (!expect_challenge.IsEmpty() && p.challenge_id != expect_challenge) {
		seterr("challenge mismatch");
		return false;
	}

	int64 now = NowSec();
	int64 diff = now >= p.ts ? (now - p.ts) : (p.ts - now);
	if (diff > time_window_sec_) {
		seterr("login code expired");
		return false;
	}

	IAdminNonceStore* store = nonce_store_ ? nonce_store_ : &default_store_;
	String scope = BuildScopeKey(p.username_lc, p.challenge_id, p.nonce);
	if (!store->TryUse(scope, p.ts + time_window_sec_)) {
		seterr("replay detected");
		return false;
	}

	if (out_payload)
		*out_payload = p;
	seterr("");
	return true;
}

}
