#include "../AccountServer.h"

namespace AccountServer {

int64_t TicketIssuer::NowSec()
{
	// U++ 提供的 UTC 秒级时间戳，与库里 bigint 时间字段一致
	return Upp::GetUTCSeconds(Upp::GetSysTime());
}

void TicketIssuer::SetKey(TicketType t, const String& key)
{
	switch (t) {
	case TicketType::LOGIN: key_login_  = key; break;
	case TicketType::ZONE:  key_zone_   = key; break;
	case TicketType::ENTER: key_enter_  = key; break;
	}
}

bool TicketIssuer::HasKey(TicketType t) const
{
	return !GetKey(t).IsEmpty();
}

const String& TicketIssuer::GetKey(TicketType t) const
{
	switch (t) {
	case TicketType::LOGIN: return key_login_;
	case TicketType::ZONE:  return key_zone_;
	case TicketType::ENTER: return key_enter_;
	}
	return key_login_;
}

// 固定 34 字节，全部大端，跨平台一致
void TicketIssuer::SerializePayload(const TicketPayload& p, String& out)
{
	out.Clear();
	auto put8 = [&](uint8_t v) { out.Cat((char)v); };
	auto put32 = [&](uint32_t v) {
		for (int i = 3; i >= 0; i--) out.Cat((char)((v >> (i * 8)) & 0xFF));
	};
	auto put64 = [&](uint64_t v) {
		for (int i = 7; i >= 0; i--) out.Cat((char)((v >> (i * 8)) & 0xFF));
	};

	put8(p.ver);
	put8(p.type);
	put64(p.uid);
	put32(p.zone_id);
	put32(p.line_id);
	put64((uint64_t)p.expire);
	put64(p.nonce);
}

bool TicketIssuer::DeserializePayload(const String& raw, TicketPayload& out)
{
	if (raw.GetCount() != PAYLOAD_LEN)
		return false;
	const byte* p = (const byte*)raw.Begin();

	auto get32 = [&](int off) -> uint32_t {
		return ((uint32_t)p[off] << 24) | ((uint32_t)p[off + 1] << 16)
		     | ((uint32_t)p[off + 2] << 8) | (uint32_t)p[off + 3];
	};
	auto get64 = [&](int off) -> uint64_t {
		uint64_t v = 0;
		for (int i = 0; i < 8; i++)
			v = (v << 8) | (uint64_t)p[off + i];
		return v;
	};

	out.ver     = p[0];
	out.type    = p[1];
	out.uid     = get64(2);
	out.zone_id = get32(10);
	out.line_id = get32(14);
	out.expire  = (int64_t)get64(18);
	out.nonce   = get64(26);
	return true;
}

String TicketIssuer::Issue(TicketType t, uint64_t uid, uint32_t zone_id,
                           uint32_t line_id, int64_t ttl_sec)
{
	const String& key = GetKey(t);
	if (key.IsEmpty())
		return String();                      // 未配置密钥，拒绝签发

	TicketPayload p;
	p.ver     = 1;
	p.type    = (uint8_t)t;
	p.uid     = uid;
	p.zone_id = zone_id;
	p.line_id = line_id;
	p.expire  = NowSec() + (ttl_sec > 0 ? ttl_sec : 60);

	// ★ nonce 必须用 CSPRNG，不能用 U++ 的 Random()
	uint64_t nonce = 0;
	if (RAND_bytes((unsigned char*)&nonce, sizeof(nonce)) != 1)
		return String();
	p.nonce = nonce;

	String payload;
	SerializePayload(p, payload);

	unsigned char mac[SIG_LEN];
	unsigned int  maclen = 0;
	if (!HMAC(EVP_sha256(), key.Begin(), key.GetCount(),
	          (const unsigned char*)payload.Begin(), payload.GetCount(), mac, &maclen))
		return String();
	if (maclen != (unsigned)SIG_LEN)
		return String();

	String raw = payload;
	raw.Cat((const char*)mac, SIG_LEN);
	return Upp::Base64Encode(raw);
}

bool TicketIssuer::Verify(TicketType t, const String& ticket, TicketPayload& out)
{
	const String& key = GetKey(t);
	if (key.IsEmpty() || ticket.IsEmpty())
		return false;

	String raw = Upp::Base64Decode(ticket);
	if (raw.GetCount() != PAYLOAD_LEN + SIG_LEN)
		return false;

	String payload = raw.Mid(0, PAYLOAD_LEN);
	String sig     = raw.Mid(PAYLOAD_LEN, SIG_LEN);

	unsigned char mac[SIG_LEN];
	unsigned int  maclen = 0;
	if (!HMAC(EVP_sha256(), key.Begin(), key.GetCount(),
	          (const unsigned char*)payload.Begin(), payload.GetCount(), mac, &maclen))
		return false;
	if (maclen != (unsigned)SIG_LEN)
		return false;

	// ★ 常数时间比较，禁止用 memcmp（会短路泄露前缀匹配长度）
	if (CRYPTO_memcmp(mac, (const void*)sig.Begin(), SIG_LEN) != 0)
		return false;

	if (!DeserializePayload(payload, out))
		return false;
	if (out.ver != 1)
		return false;
	// ★ 类型校验：防止低等级票据被当成高等级使用
	if ((TicketType)out.type != t)
		return false;
	// 过期校验
	if (NowSec() > out.expire)
		return false;

	return true;
}

}
