#pragma once
// ============================================================================
// Ticket.h —— 登录票据签发与验签
//
// 设计要点：
//   1. 票据自包含，Gate / Game 本地验签，不需要向 AccountServer 查询
//   2. 每级服务持有不同密钥：Gate 只有 ZONE 密钥，即便被攻破也伪造不了 ENTER 票据
//   3. 验签必须走常数时间比较（CRYPTO_memcmp），禁止 memcmp
//   4. 必须校验 type，防止 login_ticket 被当 zone_ticket 使用
//
// 票据格式（下发前整体 base64）：
//   [ver:1][type:1][uid:8 BE][zone_id:4 BE][line_id:4 BE][expire:8 BE][nonce:8 BE]
//   = 34 字节，后接 HMAC-SHA256 签名 32 字节
// ============================================================================


namespace AccountServer {

	enum class TicketType : uint8_t
	{
		LOGIN = 1,      // 选区用，AccountServer 自验
		ZONE = 2,      // 连接 Gate 用，Gate 本地验签
		ENTER = 3,      // 进入 Game 用，Game 本地验签
	};

	struct TicketPayload
	{
		uint8_t  ver = 1;
		uint8_t  type = 0;
		uint64_t uid = 0;
		uint32_t zone_id = 0;
		uint32_t line_id = 0;
		int64_t  expire = 0;      // Unix 秒级时间戳
		uint64_t nonce = 0;      // 随机数，防重放
	};

	class TicketIssuer
	{
		typedef TicketIssuer CLASSNAME;
	public:
		static const int PAYLOAD_LEN = 34;
		static const int SIG_LEN = 32;

		// 设置各类型密钥。启动时从配置文件读入，不要写死在代码里
		void SetKey(TicketType t, const String& key);
		bool HasKey(TicketType t) const;

		// 签发。返回 base64 字符串，失败返回空串
		String Issue(TicketType t, uint64_t uid, uint32_t zone_id, uint32_t line_id, int64_t ttl_sec);

		// 验签。成功返回 true 并填充 out
		bool Verify(TicketType t, const String& ticket, TicketPayload& out);

		// 当前 Unix 秒
		static int64_t NowSec();

	private:
		static void SerializePayload(const TicketPayload& p, String& out);
		static bool DeserializePayload(const String& raw, TicketPayload& out);
		const String& GetKey(TicketType t) const;

		String key_login_;
		String key_zone_;
		String key_enter_;
	};

}
