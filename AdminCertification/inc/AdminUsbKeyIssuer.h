#pragma once

namespace AdminCertification {

	/// <summary>
	/// Key 文件解密后的业务载荷。
	/// 用于 EXE 或服务端在 VerifyKeyFile 成功后获取绑定信息。
	/// </summary>
	struct AdminKeyPayload
	{
		int     ver = 1;
		String  username_lc;
		String  usb_serial;
		String  device_secret;
		int64   issued_at = 0;
		int64   expire_at = 0;
		uint64  nonce = 0;
	};
	/// <summary>
	/// 可移动存储设备信息。
	/// </summary>
	struct RemovableDriveInfo : Moveable<RemovableDriveInfo> {
		String root;
		String device_serial;
		RemovableDriveInfo(Tuple2<String, String> t) : root(t.a), device_serial(t.b) {}
		RemovableDriveInfo(const String& r, const String& s) : root(r), device_serial(s) {}

	};
	/// <summary>
	/// 超级后台 U 盘 Key 签发器。
	///
	/// 设计目标：
	/// 1) 根据 用户名 + 密码 + U 盘序列号 生成可落盘的 key 文本。
	/// 2) 外部 EXE 读取 key 后，必须输入账号密码且插入对应 U 盘才能解密通过。
	/// 3) key 文本被篡改、过期、账号不匹配、U 盘序列号不匹配时都会校验失败。
	///
	/// 推荐使用流程（服务端签发）：
	///   AdminUsbKeyIssuer issuer;
	///   issuer.SetServerSecret("从配置读取的高强度密钥");
	///   String err;
	///   String keyfile = issuer.IssueKeyFile(username, password, usbSerial, 30 * 24 * 3600, &err);
	///   // keyfile 写入 U 盘文件，例如 admin.key
	///
	/// 推荐使用流程（客户端 EXE 校验）：
	///   AdminKeyPayload payload;
	///   String err;
	///   bool ok = issuer.VerifyKeyFile(keyfileText, username, password, realUsbSerial, &payload, &err);
	///   // ok=true 才允许生成登录码
	/// </summary>
	class AdminUsbKeyIssuer
	{
	public:
		/// <summary>PBKDF2 盐长度（字节）。</summary>
		static const int SALT_LEN = 16;
		/// <summary>AES-GCM IV 长度（字节）。</summary>
		static const int IV_LEN = 12;
		/// <summary>AES-GCM Tag 长度（字节）。</summary>
		static const int TAG_LEN = 16;
		/// <summary>HMAC-SHA256 输出长度（字节）。</summary>
		static const int SIG_LEN = 32;
		/// <summary>PBKDF2 默认迭代次数。</summary>
		static const int DEFAULT_PBKDF2_ITER = 210000;

		/// <summary>
		/// 设置服务端主密钥（用于 key 文件整体签名）。
		/// 要求：从配置文件读取，不要硬编码在源码。
		/// </summary>
		void SetServerSecret(const String& secret);
		/// <summary>是否已设置服务端主密钥。</summary>
		bool HasServerSecret() const;

		/// <summary>
		/// 签发 key 文件文本（Base64）。
		/// 参数说明：
		/// - username: 管理员用户名（会内部标准化为小写）
		/// - password: 管理员输入密码（用于派生加密密钥）
		/// - usb_serial: U 盘硬件序列号（会内部标准化为大写）
		/// - ttl_sec: key 有效期（秒）
		/// - error: 失败时返回错误信息
		/// - issued_device_secret: 可选输出参数，返回本次签发绑定的设备密钥（建议服务端安全存储）
		/// 返回：成功返回可直接写入文件的文本，失败返回空串。
		/// </summary>
		String IssueKeyFile(const String& username,
			const String& password,
			const String& usb_serial,
			int64 ttl_sec = 30 * 24 * 3600,
			String* error = nullptr,
			String* issued_device_secret = nullptr) const;

		/// <summary>
		/// 校验并解密 key 文件。
		/// 校验内容包括：签名、过期时间、用户名一致性、U 盘序列号一致性、密码正确性。
		/// 参数说明：
		/// - keyfile: key 文件文本（支持原始二进制或 Base64 文本）
		/// - username/password/usb_serial: EXE 当前输入与读取到的真实值
		/// - out: 成功时输出解密后的业务载荷
		/// - error: 失败原因
		/// - out_device_secret: 可选输出参数，返回解密后的设备密钥（用于登录码验签）
		/// 返回：全部通过返回 true，否则 false。
		/// </summary>
		bool VerifyKeyFile(const String& keyfile,
			const String& username,
			const String& password,
			const String& usb_serial,
			AdminKeyPayload* out = nullptr,
			String* error = nullptr,
			String* out_device_secret = nullptr) const;

		/// <summary>获取当前 UTC 秒级时间戳。</summary>
		static int64 NowSec();

#ifdef _WIN32
		static bool ReadDeviceSerialByRoot(const String& root, String& serial);
#elif defined(PLATFORM_POSIX)
		static String ReadTextFileTrim(const String& path);
		static String BlockBaseNameFromDevice(const String& devpath);
		static bool IsRemovableOrUsbBlock(const String& base);
		static String ReadLinuxDeviceSerial(const String& base, const String& devpath);
#endif
		static Vector<RemovableDriveInfo> EnumerateRemovableDrives();
	private:
		/// <summary>服务端主密钥（用于 key 外层 HMAC 防篡改）。</summary>
		String server_secret_;

		/// <summary>用户名标准化：Trim + ToLower。</summary>
		static String NormalizeUsername(const String& s);
		/// <summary>U 盘序列号标准化：Trim + ToUpper。</summary>
		static String NormalizeUsbSerial(const String& s);

		/// <summary>按大端写入 16 位整数。</summary>
		static void PutU16(String& out, uint16 v);
		/// <summary>按大端写入 32 位整数。</summary>
		static void PutU32(String& out, uint32 v);
		/// <summary>按大端写入 64 位整数。</summary>
		static void PutU64(String& out, uint64 v);
		/// <summary>按大端读取 16 位整数。</summary>
		static bool GetU16(const String& in, int& off, uint16& v);
		/// <summary>按大端读取 32 位整数。</summary>
		static bool GetU32(const String& in, int& off, uint32& v);
		/// <summary>按大端读取 64 位整数。</summary>
		static bool GetU64(const String& in, int& off, uint64& v);

		/// <summary>生成密码学安全随机字节（RAND_bytes）。</summary>
		static bool RandomBytes(String& out, int len);
		/// <summary>PBKDF2-HMAC-SHA256 派生 32 字节密钥。</summary>
		static bool DeriveKey(const String& password, const String& salt, int iterations, String& out32);
		/// <summary>AES-256-GCM 加密，输出密文和认证 Tag。</summary>
		static bool EncryptAes256Gcm(const String& key32, const String& iv12, const String& plain, String& cipher, String& tag16);
		/// <summary>AES-256-GCM 解密并校验 Tag。</summary>
		static bool DecryptAes256Gcm(const String& key32, const String& iv12, const String& cipher, const String& tag16, String& plain);
		/// <summary>计算 HMAC-SHA256（用于整个 key 文件防篡改）。</summary>
		static bool HmacSha256(const String& key, const String& data, String& out32);

		/// <summary>构建加密前的业务载荷（二进制格式）。</summary>
		static String BuildPlainPayload(const String& username_lc,
			const String& usb_serial,
			const String& device_secret,
			int64 issued_at,
			int64 expire_at,
			uint64 nonce);
		/// <summary>解析加密前业务载荷（二进制格式）。</summary>
		static bool ParsePlainPayload(const String& plain, AdminKeyPayload& out);


	};

}
