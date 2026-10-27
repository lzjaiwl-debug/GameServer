
namespace AssetLib {
	namespace ServerConfigLicense {

		/*
		 * ServerConfigLicense
		 * ===================
		 *
		 * 设计目标
		 * --------
		 * 1) 将服务器明文 JSON 配置加密为“授权配置文件”；
		 * 2) 授权配置与目标机器硬件序列号绑定；
		 * 3) 运行时仅授权机器可解密并还原 JSON，未授权机器启动失败。
		 *
		 * 不依赖说明
		 * -----------
		 * - 本头文件仅依赖：C++标准库 + OpenSSL + Windows/Linux 系统API；
		 * - 不依赖 U++ 类型，接口参数统一使用 std::string / std::string_view。
		 *
		 * 推荐使用方式
		 * -----------
		 * 1) 在签发工具侧：
		 *      std::string sealed;
		 *      std::string err;
		 *      bool ok = SealConfigJson(json_text, target_serial, sealed, &err);
		 *      if(ok) SaveSealedConfigFile("AccountServer.config", sealed, &err);
		 *
		 * 2) 在服务端启动侧：
		 *      std::string json;
		 *      std::string err;
		 *      bool ok = LoadAndUnsealLocalConfigFile("AccountServer.config", json, nullptr, nullptr, &err);
		 *      // ok=false 代表非授权机器或配置损坏，必须拒绝启动
		 *
		 * 3) 查询当前机器硬件信息：
		 *      std::string code, serial, err;
		 *      GetLocalHardwareSerial(serial, &code, &err);
		 */

		/// AES-256 对称密钥长度（字节）
		inline constexpr int KEY_LEN = 32;
		/// SHA-256 摘要长度（字节）
		inline constexpr int SERIAL_HASH_LEN = 32;
		/// PBKDF2 盐长度（字节）
		inline constexpr int SALT_LEN = 16;
		/// AES-GCM IV 长度（字节）
		inline constexpr int IV_LEN = 12;
		/// AES-GCM Tag 长度（字节）
		inline constexpr int TAG_LEN = 16;
		/// 默认 PBKDF2 迭代次数
		inline constexpr int DEFAULT_PBKDF2_ITER = 210000;
		/// PBKDF2 迭代次数最小值（避免被降级）
		inline constexpr int MIN_PBKDF2_ITER = 10000;
		/// PBKDF2 迭代次数最大值（避免异常配置拖垮性能）
		inline constexpr int MAX_PBKDF2_ITER = 3000000;

		///
		/// @brief 统一设置错误字符串（允许 error 为空）
		/// @param error 外部错误输出指针
		/// @param value 要写入的错误信息
		///
		inline void SetError(std::string* error, const std::string& value)
		{
			if (error)
				*error = value;
		}

		///
		/// @brief 去除字符串首尾空白字符（空格、\r、\n、\t 等）
		/// @param input 输入视图
		/// @return 去除首尾空白后的新字符串
		///
		inline std::string Trim(std::string_view input)
		{
			size_t begin = 0;
			while (begin < input.size() && std::isspace(static_cast<unsigned char>(input[begin])) != 0)
				++begin;

			size_t end = input.size();
			while (end > begin && std::isspace(static_cast<unsigned char>(input[end - 1])) != 0)
				--end;

			return std::string(input.substr(begin, end - begin));
		}

		///
		/// @brief 将 ASCII 字符串转为大写（非 ASCII 字符保持原样）
		/// @param input 输入视图
		/// @return 大写后的新字符串
		///
		inline std::string ToUpperAscii(std::string_view input)
		{
			std::string out(input.begin(), input.end());
			std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
				return static_cast<char>(std::toupper(c));
				});
			return out;
		}

		///
		/// @brief 统一硬件字段规范化（Trim + ASCII大写）
		/// @param input 原始硬件字段
		/// @return 规范化后的字符串
		///
		inline std::string NormalizeHardwareToken(std::string_view input)
		{
			return ToUpperAscii(Trim(input));
		}

		///
		/// @brief 将二进制字节流转为大写十六进制字符串
		/// @param bytes 二进制输入
		/// @return 十六进制大写字符串
		///
		inline std::string BytesToHexUpper(std::string_view bytes)
		{
			static const char* kHex = "0123456789ABCDEF";
			std::string out;
			out.reserve(bytes.size() * 2);
			for (unsigned char b : bytes) {
				out.push_back(kHex[(b >> 4) & 0x0F]);
				out.push_back(kHex[b & 0x0F]);
			}
			return out;
		}

		///
		/// @brief 以大端序写入 uint32 到缓冲区
		/// @param out 目标缓冲区
		/// @param v 要写入的值
		///
		inline void PutU32(std::string& out, uint32_t v)
		{
			for (int i = 3; i >= 0; --i)
				out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
		}

		///
		/// @brief 以大端序写入 uint64 到缓冲区
		/// @param out 目标缓冲区
		/// @param v 要写入的值
		///
		inline void PutU64(std::string& out, uint64_t v)
		{
			for (int i = 7; i >= 0; --i)
				out.push_back(static_cast<char>((v >> (i * 8)) & 0xFF));
		}

		///
		/// @brief 以大端序从缓冲区读取 uint32
		/// @param in 输入缓冲区
		/// @param off [in/out] 当前偏移，读取成功后自动后移4字节
		/// @param v 输出值
		/// @return 成功返回 true，越界返回 false
		///
		inline bool GetU32(const std::string& in, size_t& off, uint32_t& v)
		{
			if (off + 4 > in.size())
				return false;
			const unsigned char* p = reinterpret_cast<const unsigned char*>(in.data());
			v = ((uint32_t)p[off] << 24)
				| ((uint32_t)p[off + 1] << 16)
				| ((uint32_t)p[off + 2] << 8)
				| (uint32_t)p[off + 3];
			off += 4;
			return true;
		}

		///
		/// @brief 以大端序从缓冲区读取 uint64
		/// @param in 输入缓冲区
		/// @param off [in/out] 当前偏移，读取成功后自动后移8字节
		/// @param v 输出值
		/// @return 成功返回 true，越界返回 false
		///
		inline bool GetU64(const std::string& in, size_t& off, uint64_t& v)
		{
			if (off + 8 > in.size())
				return false;
			const unsigned char* p = reinterpret_cast<const unsigned char*>(in.data());
			v = 0;
			for (int i = 0; i < 8; ++i)
				v = (v << 8) | static_cast<uint64_t>(p[off + i]);
			off += 8;
			return true;
		}

		///
		/// @brief 常数时间比较，防止基于比较时间的侧信道攻击
		/// @param a 待比较字符串A
		/// @param b 待比较字符串B
		/// @return 完全一致返回 true，否则 false
		///
		inline bool ConstantTimeEqual(std::string_view a, std::string_view b)
		{
			if (a.size() != b.size())
				return false;
			if (a.empty())
				return true;
			return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
		}

		///
		/// @brief 生成密码学安全随机字节
		/// @param out 输出缓冲区
		/// @param len 期望字节数
		/// @return 成功返回 true
		///
		inline bool RandomBytes(std::string& out, int len)
		{
			out.clear();
			if (len <= 0)
				return false;
			std::vector<unsigned char> bytes(static_cast<size_t>(len));
			if (RAND_bytes(bytes.data(), len) != 1)
				return false;
			out.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
			return true;
		}

		///
		/// @brief 计算 SHA-256 摘要
		/// @param data 输入数据
		/// @param out32 输出32字节摘要（二进制）
		/// @return 成功返回 true
		///
		inline bool Sha256(std::string_view data, std::string& out32)
		{
			out32.clear();
			EVP_MD_CTX* md = EVP_MD_CTX_new();
			if (!md)
				return false;

			bool ok = false;
			do {
				if (EVP_DigestInit_ex(md, EVP_sha256(), nullptr) != 1)
					break;
				if (EVP_DigestUpdate(md, data.data(), data.size()) != 1)
					break;
				unsigned char digest[EVP_MAX_MD_SIZE];
				unsigned int digest_len = 0;
				if (EVP_DigestFinal_ex(md, digest, &digest_len) != 1)
					break;
				if (digest_len != static_cast<unsigned int>(SERIAL_HASH_LEN))
					break;
				out32.assign(reinterpret_cast<const char*>(digest), static_cast<size_t>(digest_len));
				ok = true;
			} while (0);

			EVP_MD_CTX_free(md);
			return ok;
		}

		///
		/// @brief 使用 PBKDF2-HMAC-SHA256 从硬件序列号派生 32 字节密钥
		/// @param hardware_serial 规范化后的硬件序列号
		/// @param salt 随机盐值（二进制）
		/// @param iterations 迭代次数
		/// @param out32 输出 32 字节派生密钥（二进制）
		/// @return 成功返回 true
		///
		inline bool DeriveKey(std::string_view hardware_serial,
			std::string_view salt,
			int iterations,
			std::string& out32)
		{
			out32.clear();
			if (hardware_serial.empty() || salt.empty())
				return false;
			if (iterations < MIN_PBKDF2_ITER || iterations > MAX_PBKDF2_ITER)
				return false;

			std::vector<unsigned char> key(static_cast<size_t>(KEY_LEN));
			int ok = PKCS5_PBKDF2_HMAC(hardware_serial.data(), static_cast<int>(hardware_serial.size()),
				reinterpret_cast<const unsigned char*>(salt.data()), static_cast<int>(salt.size()),
				iterations, EVP_sha256(), static_cast<int>(key.size()), key.data());
			if (ok != 1)
				return false;

			out32.assign(reinterpret_cast<const char*>(key.data()), key.size());
			return true;
		}

		///
		/// @brief 使用 AES-256-GCM 加密配置明文
		/// @param key32 32字节对称密钥
		/// @param iv12 12字节随机IV
		/// @param plain 待加密明文
		/// @param cipher 输出密文（二进制）
		/// @param tag16 输出16字节认证Tag
		/// @return 成功返回 true
		///
		inline bool EncryptAes256Gcm(std::string_view key32,
			std::string_view iv12,
			std::string_view plain,
			std::string& cipher,
			std::string& tag16)
		{
			cipher.clear();
			tag16.clear();
			if (key32.size() != static_cast<size_t>(KEY_LEN) || iv12.size() != static_cast<size_t>(IV_LEN))
				return false;

			EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
			if (!ctx)
				return false;

			bool ok = false;
			do {
				if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
					break;
				if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv12.size()), nullptr) != 1)
					break;
				if (EVP_EncryptInit_ex(ctx, nullptr, nullptr,
					reinterpret_cast<const unsigned char*>(key32.data()),
					reinterpret_cast<const unsigned char*>(iv12.data())) != 1)
					break;

				std::vector<unsigned char> out(plain.size() + static_cast<size_t>(TAG_LEN));
				int out_len = 0;
				if (EVP_EncryptUpdate(ctx, out.data(), &out_len,
					reinterpret_cast<const unsigned char*>(plain.data()), static_cast<int>(plain.size())) != 1)
					break;
				int final_len = 0;
				if (EVP_EncryptFinal_ex(ctx, out.data() + out_len, &final_len) != 1)
					break;
				out_len += final_len;

				unsigned char tag[TAG_LEN];
				if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, tag) != 1)
					break;

				cipher.assign(reinterpret_cast<const char*>(out.data()), static_cast<size_t>(out_len));
				tag16.assign(reinterpret_cast<const char*>(tag), static_cast<size_t>(TAG_LEN));
				ok = true;
			} while (0);

			EVP_CIPHER_CTX_free(ctx);
			return ok;
		}

		///
		/// @brief 使用 AES-256-GCM 解密并校验 Tag
		/// @param key32 32字节对称密钥
		/// @param iv12 12字节IV
		/// @param cipher 密文（二进制）
		/// @param tag16 16字节认证Tag
		/// @param plain 输出明文
		/// @return 成功返回 true；校验失败/密钥错误返回 false
		///
		inline bool DecryptAes256Gcm(std::string_view key32,
			std::string_view iv12,
			std::string_view cipher,
			std::string_view tag16,
			std::string& plain)
		{
			plain.clear();
			if (key32.size() != static_cast<size_t>(KEY_LEN)
				|| iv12.size() != static_cast<size_t>(IV_LEN)
				|| tag16.size() != static_cast<size_t>(TAG_LEN)) {
				return false;
			}

			EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
			if (!ctx)
				return false;

			bool ok = false;
			do {
				if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr, nullptr, nullptr) != 1)
					break;
				if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, static_cast<int>(iv12.size()), nullptr) != 1)
					break;
				if (EVP_DecryptInit_ex(ctx, nullptr, nullptr,
					reinterpret_cast<const unsigned char*>(key32.data()),
					reinterpret_cast<const unsigned char*>(iv12.data())) != 1)
					break;

				std::vector<unsigned char> out(cipher.size() + static_cast<size_t>(TAG_LEN));
				int out_len = 0;
				if (EVP_DecryptUpdate(ctx, out.data(), &out_len,
					reinterpret_cast<const unsigned char*>(cipher.data()), static_cast<int>(cipher.size())) != 1)
					break;
				if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, const_cast<char*>(tag16.data())) != 1)
					break;
				int final_len = 0;
				if (EVP_DecryptFinal_ex(ctx, out.data() + out_len, &final_len) != 1)
					break;
				out_len += final_len;

				plain.assign(reinterpret_cast<const char*>(out.data()), static_cast<size_t>(out_len));
				ok = true;
			} while (0);

			EVP_CIPHER_CTX_free(ctx);
			return ok;
		}

		///
		/// @brief Base64 编码（标准表）
		/// @param input 输入二进制数据
		/// @return Base64 文本
		///
		inline std::string Base64Encode(std::string_view input)
		{
			static const char kTable[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
			std::string out;
			out.reserve(((input.size() + 2) / 3) * 4);

			const unsigned char* p = reinterpret_cast<const unsigned char*>(input.data());
			size_t i = 0;
			for (; i + 3 <= input.size(); i += 3) {
				unsigned int v = (static_cast<unsigned int>(p[i]) << 16)
					| (static_cast<unsigned int>(p[i + 1]) << 8)
					| static_cast<unsigned int>(p[i + 2]);
				out.push_back(kTable[(v >> 18) & 0x3F]);
				out.push_back(kTable[(v >> 12) & 0x3F]);
				out.push_back(kTable[(v >> 6) & 0x3F]);
				out.push_back(kTable[v & 0x3F]);
			}

			const size_t rem = input.size() - i;
			if (rem == 1) {
				unsigned int v = static_cast<unsigned int>(p[i]) << 16;
				out.push_back(kTable[(v >> 18) & 0x3F]);
				out.push_back(kTable[(v >> 12) & 0x3F]);
				out.push_back('=');
				out.push_back('=');
			}
			else if (rem == 2) {
				unsigned int v = (static_cast<unsigned int>(p[i]) << 16)
					| (static_cast<unsigned int>(p[i + 1]) << 8);
				out.push_back(kTable[(v >> 18) & 0x3F]);
				out.push_back(kTable[(v >> 12) & 0x3F]);
				out.push_back(kTable[(v >> 6) & 0x3F]);
				out.push_back('=');
			}

			return out;
		}

		///
		/// @brief Base64 解码（忽略空白字符）
		/// @param input Base64 文本
		/// @param out 输出二进制数据
		/// @return 成功返回 true，格式非法返回 false
		///
		inline bool Base64Decode(std::string_view input, std::string& out)
		{
			out.clear();
			static const std::array<unsigned char, 256> kDecode = []() {
				std::array<unsigned char, 256> tbl{};
				tbl.fill(0xFF);
				for (unsigned char i = 0; i < 26; ++i) {
					tbl[static_cast<size_t>('A' + i)] = i;
					tbl[static_cast<size_t>('a' + i)] = static_cast<unsigned char>(26 + i);
				}
				for (unsigned char i = 0; i < 10; ++i)
					tbl[static_cast<size_t>('0' + i)] = static_cast<unsigned char>(52 + i);
				tbl[static_cast<size_t>('+')] = 62;
				tbl[static_cast<size_t>('/')] = 63;
				return tbl;
				}();

			std::string clean;
			clean.reserve(input.size());
			for (char c : input) {
				if (std::isspace(static_cast<unsigned char>(c)) != 0)
					continue;
				clean.push_back(c);
			}

			if (clean.empty() || (clean.size() % 4) != 0)
				return false;

			out.reserve((clean.size() / 4) * 3);
			for (size_t i = 0; i < clean.size(); i += 4) {
				const char c0 = clean[i];
				const char c1 = clean[i + 1];
				const char c2 = clean[i + 2];
				const char c3 = clean[i + 3];

				if (c0 == '=' || c1 == '=')
					return false;

				const unsigned char v0 = kDecode[static_cast<unsigned char>(c0)];
				const unsigned char v1 = kDecode[static_cast<unsigned char>(c1)];
				if (v0 == 0xFF || v1 == 0xFF)
					return false;

				if (c2 == '=') {
					if (c3 != '=' || i + 4 != clean.size())
						return false;
					const unsigned int n = (static_cast<unsigned int>(v0) << 18)
						| (static_cast<unsigned int>(v1) << 12);
					out.push_back(static_cast<char>((n >> 16) & 0xFF));
					break;
				}

				const unsigned char v2 = kDecode[static_cast<unsigned char>(c2)];
				if (v2 == 0xFF)
					return false;

				if (c3 == '=') {
					if (i + 4 != clean.size())
						return false;
					const unsigned int n = (static_cast<unsigned int>(v0) << 18)
						| (static_cast<unsigned int>(v1) << 12)
						| (static_cast<unsigned int>(v2) << 6);
					out.push_back(static_cast<char>((n >> 16) & 0xFF));
					out.push_back(static_cast<char>((n >> 8) & 0xFF));
					break;
				}

				const unsigned char v3 = kDecode[static_cast<unsigned char>(c3)];
				if (v3 == 0xFF)
					return false;

				const unsigned int n = (static_cast<unsigned int>(v0) << 18)
					| (static_cast<unsigned int>(v1) << 12)
					| (static_cast<unsigned int>(v2) << 6)
					| static_cast<unsigned int>(v3);
				out.push_back(static_cast<char>((n >> 16) & 0xFF));
				out.push_back(static_cast<char>((n >> 8) & 0xFF));
				out.push_back(static_cast<char>(n & 0xFF));
			}

			return true;
		}

		///
		/// @brief 读取文本文件（按二进制模式读取，不做编码转换）
		/// @param path 文件路径
		/// @param out 输出内容
		/// @return 成功返回 true
		///
		inline bool ReadTextFile(const std::string& path, std::string& out)
		{
			out.clear();
			std::ifstream in(path, std::ios::binary);
			if (!in)
				return false;
			std::ostringstream ss;
			ss << in.rdbuf();
			out = ss.str();
			return in.good() || in.eof();
		}

		///
		/// @brief 写入文本文件（按二进制模式覆盖写）
		/// @param path 文件路径
		/// @param content 待写入内容
		/// @return 成功返回 true
		///
		inline bool WriteTextFile(const std::string& path, std::string_view content)
		{
			std::ofstream out(path, std::ios::binary | std::ios::trunc);
			if (!out)
				return false;
			out.write(content.data(), static_cast<std::streamsize>(content.size()));
			return out.good();
		}

#ifdef _WIN32
		///
		/// @brief 读取 Windows MachineGuid（机器级唯一标识）
		/// @param out_guid 输出 MachineGuid
		/// @return 成功返回 true
		///
		inline bool ReadWindowsMachineGuid(std::string& out_guid)
		{
			out_guid.clear();
			HKEY key = nullptr;
			LONG open_status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ | KEY_WOW64_64KEY, &key);
			if (open_status != ERROR_SUCCESS)
				open_status = RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SOFTWARE\\Microsoft\\Cryptography", 0, KEY_READ, &key);
			if (open_status != ERROR_SUCCESS || !key)
				return false;

			char buffer[256] = {};
			DWORD type = 0;
			DWORD size = static_cast<DWORD>(sizeof(buffer));
			LONG query_status = RegQueryValueExA(key, "MachineGuid", nullptr, &type, reinterpret_cast<LPBYTE>(buffer), &size);
			RegCloseKey(key);
			if (query_status != ERROR_SUCCESS || type != REG_SZ || size <= 1)
				return false;

			out_guid = Trim(buffer);
			return !out_guid.empty();
		}

		///
		/// @brief 读取 Windows 系统盘卷序列号（Volume Serial Number）
		/// @param out_serial 输出8位十六进制序列号
		/// @return 成功返回 true
		///
		inline bool ReadWindowsSystemDriveSerial(std::string& out_serial)
		{
			out_serial.clear();
			char win_dir[MAX_PATH + 1] = {};
			UINT n = GetWindowsDirectoryA(win_dir, MAX_PATH);
			if (n == 0 || n > MAX_PATH)
				return false;

			char root[] = "C:\\";
			if (((win_dir[0] >= 'A' && win_dir[0] <= 'Z') || (win_dir[0] >= 'a' && win_dir[0] <= 'z')) && win_dir[1] == ':')
				root[0] = win_dir[0];

			DWORD volume_serial = 0;
			if (!GetVolumeInformationA(root, nullptr, 0, &volume_serial, nullptr, nullptr, nullptr, 0))
				return false;

			char serial_hex[16] = {};
			std::snprintf(serial_hex, sizeof(serial_hex), "%08X", static_cast<unsigned int>(volume_serial));
			out_serial = serial_hex;
			return true;
		}
#endif

		///
		/// @brief 读取本机硬件码（跨平台）
		/// @details
		/// Windows: MachineGuid + 系统盘卷序列号 + 主机名
		/// Linux  : /etc/machine-id(或 /var/lib/dbus/machine-id) + 主机名
		/// @param out_hardware_code 输出硬件码
		/// @param error 失败原因
		/// @return 成功返回 true
		///
		inline bool GetLocalHardwareCode(std::string& out_hardware_code, std::string* error = nullptr)
		{
			out_hardware_code.clear();
#ifdef _WIN32
			std::string machine_guid;
			if (!ReadWindowsMachineGuid(machine_guid)) {
				SetError(error, "read MachineGuid failed");
				return false;
			}

			std::string drive_serial;
			if (!ReadWindowsSystemDriveSerial(drive_serial)) {
				SetError(error, "read system drive serial failed");
				return false;
			}

			char host_name[MAX_COMPUTERNAME_LENGTH + 1] = {};
			DWORD host_len = MAX_COMPUTERNAME_LENGTH + 1;
			if (!GetComputerNameA(host_name, &host_len)) {
				SetError(error, "read computer name failed");
				return false;
			}

			out_hardware_code = "MG=" + NormalizeHardwareToken(machine_guid)
				+ "|VS=" + NormalizeHardwareToken(drive_serial)
				+ "|HOST=" + NormalizeHardwareToken(host_name);
#else
			std::string machine_id;
			if (!ReadTextFile("/etc/machine-id", machine_id))
				ReadTextFile("/var/lib/dbus/machine-id", machine_id);
			machine_id = Trim(machine_id);
			if (machine_id.empty()) {
				SetError(error, "read machine-id failed");
				return false;
			}

			char host_name[256] = {};
			if (gethostname(host_name, sizeof(host_name) - 1) != 0) {
				SetError(error, "read hostname failed");
				return false;
			}

			out_hardware_code = "MID=" + NormalizeHardwareToken(machine_id)
				+ "|HOST=" + NormalizeHardwareToken(host_name);
#endif

			SetError(error, std::string());
			return true;
		}

		///
		/// @brief 根据硬件码生成硬件序列号（SHA-256 后转大写 Hex）
		/// @param hardware_code 硬件码
		/// @param error 失败原因
		/// @return 成功返回硬件序列号；失败返回空串
		///
		inline std::string BuildHardwareSerial(std::string_view hardware_code, std::string* error = nullptr)
		{
			const std::string normalized_code = NormalizeHardwareToken(hardware_code);
			if (normalized_code.empty()) {
				SetError(error, "hardware code is empty");
				return std::string();
			}

			std::string digest;
			if (!Sha256(normalized_code, digest)) {
				SetError(error, "sha256 failed");
				return std::string();
			}

			SetError(error, std::string());
			return BytesToHexUpper(digest);
		}

		///
		/// @brief 一步获取本机硬件序列号（可选返回硬件码）
		/// @param out_serial 输出硬件序列号
		/// @param out_hardware_code 可选输出硬件码
		/// @param error 失败原因
		/// @return 成功返回 true
		///
		inline bool GetLocalHardwareSerial(std::string& out_serial,
			std::string* out_hardware_code = nullptr,
			std::string* error = nullptr)
		{
			out_serial.clear();
			if (out_hardware_code)
				out_hardware_code->clear();

			std::string hardware_code;
			if (!GetLocalHardwareCode(hardware_code, error))
				return false;

			std::string hardware_serial = BuildHardwareSerial(hardware_code, error);
			if (hardware_serial.empty())
				return false;

			out_serial = hardware_serial;
			if (out_hardware_code)
				*out_hardware_code = hardware_code;
			SetError(error, std::string());
			return true;
		}

		///
		/// @brief 校验字符串是否为合法 JSON
		/// @param json_text JSON 文本
		/// @param error 失败原因
		/// @return 合法返回 true
		///
		inline bool ValidateJson(std::string_view json_text, std::string* error = nullptr)
		{
			if (Trim(json_text).empty()) {
				SetError(error, "json content is empty");
				return false;
			}

			try {
				nlohmann::json::parse(json_text.begin(), json_text.end());
			}
			catch (const nlohmann::json::parse_error& ex) {
				SetError(error, std::string("json parse error: ") + ex.what());
				return false;
			}
			catch (const nlohmann::json::exception& ex) {
				SetError(error, std::string("json error: ") + ex.what());
				return false;
			}

			SetError(error, std::string());
			return true;
		}

		///
		/// @brief 对 JSON 配置进行签发（加密并绑定硬件序列号）
		/// @param json_text 明文 JSON
		/// @param hardware_serial 目标机器硬件序列号
		/// @param out_sealed 输出签发后的 Base64 文本
		/// @param error 失败原因
		/// @param iterations PBKDF2 迭代次数
		/// @return 成功返回 true
		///
		inline bool SealConfigJson(std::string_view json_text,
			std::string_view hardware_serial,
			std::string& out_sealed,
			std::string* error = nullptr,
			int iterations = DEFAULT_PBKDF2_ITER)
		{
			out_sealed.clear();
			const std::string normalized_serial = NormalizeHardwareToken(hardware_serial);
			if (normalized_serial.empty()) {
				SetError(error, "hardware serial is empty");
				return false;
			}
			if (iterations < MIN_PBKDF2_ITER || iterations > MAX_PBKDF2_ITER) {
				SetError(error, "pbkdf2 iterations out of range");
				return false;
			}
			if (!ValidateJson(json_text, error))
				return false;

			std::string serial_hash;
			if (!Sha256(normalized_serial, serial_hash)) {
				SetError(error, "serial hash failed");
				return false;
			}

			std::string salt;
			if (!RandomBytes(salt, SALT_LEN)) {
				SetError(error, "generate salt failed");
				return false;
			}

			std::string iv;
			if (!RandomBytes(iv, IV_LEN)) {
				SetError(error, "generate iv failed");
				return false;
			}

			std::string key32;
			if (!DeriveKey(normalized_serial, salt, iterations, key32)) {
				SetError(error, "derive key failed");
				return false;
			}

			std::string cipher;
			std::string tag;
			if (!EncryptAes256Gcm(key32, iv, json_text, cipher, tag)) {
				SetError(error, "encrypt failed");
				return false;
			}

			std::string raw;
			raw.reserve(128 + cipher.size());
			raw.append("GSCF", 4);
			raw.push_back(static_cast<char>(1));
			PutU32(raw, static_cast<uint32_t>(iterations));
			raw.push_back(static_cast<char>(SALT_LEN));
			raw.push_back(static_cast<char>(IV_LEN));
			raw.push_back(static_cast<char>(TAG_LEN));
			raw.push_back(static_cast<char>(SERIAL_HASH_LEN));
			PutU64(raw, static_cast<uint64_t>(std::time(nullptr)));
			PutU32(raw, static_cast<uint32_t>(cipher.size()));
			raw.append(serial_hash);
			raw.append(salt);
			raw.append(iv);
			raw.append(tag);
			raw.append(cipher);

			out_sealed = Base64Encode(raw);
			SetError(error, std::string());
			return true;
		}

		///
		/// @brief 解密签发配置并还原 JSON（需同一硬件序列号）
		/// @param sealed_text 签发文件文本（Base64 或原始二进制串）
		/// @param hardware_serial 当前机器硬件序列号
		/// @param out_json 输出还原后的 JSON 明文
		/// @param error 失败原因
		/// @return 成功返回 true
		///
		inline bool UnsealConfigJson(std::string_view sealed_text,
			std::string_view hardware_serial,
			std::string& out_json,
			std::string* error = nullptr)
		{
			out_json.clear();
			const std::string normalized_serial = NormalizeHardwareToken(hardware_serial);
			if (normalized_serial.empty()) {
				SetError(error, "hardware serial is empty");
				return false;
			}

			const std::string text = Trim(sealed_text);
			if (text.empty()) {
				SetError(error, "sealed config is empty");
				return false;
			}

			std::string raw;
			if (text.size() >= 4 && std::memcmp(text.data(), "GSCF", 4) == 0)
				raw = text;
			else if (!Base64Decode(text, raw)) {
				SetError(error, "base64 decode failed");
				return false;
			}

			const size_t min_len = 4 + 1 + 4 + 1 + 1 + 1 + 1 + 8 + 4
				+ static_cast<size_t>(SERIAL_HASH_LEN)
				+ static_cast<size_t>(SALT_LEN)
				+ static_cast<size_t>(IV_LEN)
				+ static_cast<size_t>(TAG_LEN);
			if (raw.size() < min_len) {
				SetError(error, "sealed config length invalid");
				return false;
			}

			if (std::memcmp(raw.data(), "GSCF", 4) != 0) {
				SetError(error, "sealed config magic invalid");
				return false;
			}

			size_t off = 4;
			const int version = static_cast<unsigned char>(raw[off++]);
			if (version != 1) {
				SetError(error, "sealed config version invalid");
				return false;
			}

			uint32_t iterations = 0;
			if (!GetU32(raw, off, iterations)) {
				SetError(error, "parse iterations failed");
				return false;
			}
			if (iterations < MIN_PBKDF2_ITER || iterations > MAX_PBKDF2_ITER) {
				SetError(error, "pbkdf2 iterations invalid");
				return false;
			}

			if (off + 4 > raw.size()) {
				SetError(error, "parse crypto lengths failed");
				return false;
			}

			const int salt_len = static_cast<unsigned char>(raw[off++]);
			const int iv_len = static_cast<unsigned char>(raw[off++]);
			const int tag_len = static_cast<unsigned char>(raw[off++]);
			const int serial_hash_len = static_cast<unsigned char>(raw[off++]);

			if (salt_len != SALT_LEN || iv_len != IV_LEN || tag_len != TAG_LEN || serial_hash_len != SERIAL_HASH_LEN) {
				SetError(error, "crypto length metadata mismatch");
				return false;
			}

			uint64_t issued_at = 0;
			if (!GetU64(raw, off, issued_at)) {
				SetError(error, "parse issued_at failed");
				return false;
			}
			(void)issued_at;

			uint32_t cipher_len = 0;
			if (!GetU32(raw, off, cipher_len)) {
				SetError(error, "parse cipher length failed");
				return false;
			}
			if (cipher_len == 0) {
				SetError(error, "cipher length invalid");
				return false;
			}

			const size_t expect_len = off + static_cast<size_t>(serial_hash_len)
				+ static_cast<size_t>(salt_len)
				+ static_cast<size_t>(iv_len)
				+ static_cast<size_t>(tag_len)
				+ static_cast<size_t>(cipher_len);
			if (expect_len != raw.size()) {
				SetError(error, "sealed config body length mismatch");
				return false;
			}

			const std::string serial_hash_in_file = raw.substr(off, static_cast<size_t>(serial_hash_len));
			off += static_cast<size_t>(serial_hash_len);
			const std::string salt = raw.substr(off, static_cast<size_t>(salt_len));
			off += static_cast<size_t>(salt_len);
			const std::string iv = raw.substr(off, static_cast<size_t>(iv_len));
			off += static_cast<size_t>(iv_len);
			const std::string tag = raw.substr(off, static_cast<size_t>(tag_len));
			off += static_cast<size_t>(tag_len);
			const std::string cipher = raw.substr(off, static_cast<size_t>(cipher_len));

			std::string expected_serial_hash;
			if (!Sha256(normalized_serial, expected_serial_hash)) {
				SetError(error, "serial hash failed");
				return false;
			}
			if (!ConstantTimeEqual(expected_serial_hash, serial_hash_in_file)) {
				SetError(error, "hardware serial mismatch");
				return false;
			}

			std::string key32;
			if (!DeriveKey(normalized_serial, salt, static_cast<int>(iterations), key32)) {
				SetError(error, "derive key failed");
				return false;
			}

			std::string plain;
			if (!DecryptAes256Gcm(key32, iv, cipher, tag, plain)) {
				SetError(error, "decrypt failed");
				return false;
			}

			if (!ValidateJson(plain, error))
				return false;

			out_json = std::move(plain);
			SetError(error, std::string());
			return true;
		}

		///
		/// @brief 从本地文件读取并解密授权配置（自动获取本机硬件信息）
		/// @param config_file_path 配置文件路径
		/// @param out_json 输出解密后的 JSON
		/// @param out_hardware_serial 可选输出当前机器硬件序列号
		/// @param out_hardware_code 可选输出当前机器硬件码
		/// @param error 失败原因
		/// @return 成功返回 true
		///
		inline bool LoadAndUnsealLocalConfigFile(const std::string& config_file_path,
			std::string& out_json,
			std::string* out_hardware_serial = nullptr,
			std::string* out_hardware_code = nullptr,
			std::string* error = nullptr)
		{
			out_json.clear();
			if (out_hardware_serial)
				out_hardware_serial->clear();
			if (out_hardware_code)
				out_hardware_code->clear();

			if (config_file_path.empty()) {
				SetError(error, "config file path is empty");
				return false;
			}

			std::string sealed_text;
			if (!ReadTextFile(config_file_path, sealed_text)) {
				SetError(error, "read config file failed");
				return false;
			}
			if (Trim(sealed_text).empty()) {
				SetError(error, "config file is empty");
				return false;
			}

			std::string hardware_code;
			std::string hardware_serial;
			if (!GetLocalHardwareSerial(hardware_serial, &hardware_code, error))
				return false;

			if (!UnsealConfigJson(sealed_text, hardware_serial, out_json, error))
				return false;

			if (out_hardware_serial)
				*out_hardware_serial = hardware_serial;
			if (out_hardware_code)
				*out_hardware_code = hardware_code;
			SetError(error, std::string());
			return true;
		}

		///
		/// @brief 将签发文本保存到指定文件
		/// @param output_path 输出路径
		/// @param sealed_text 签发文本
		/// @param error 失败原因
		/// @return 成功返回 true
		///
		inline bool SaveSealedConfigFile(const std::string& output_path,
			std::string_view sealed_text,
			std::string* error = nullptr)
		{
			if (output_path.empty()) {
				SetError(error, "output path is empty");
				return false;
			}
			if (sealed_text.empty()) {
				SetError(error, "sealed text is empty");
				return false;
			}

			if (!WriteTextFile(output_path, sealed_text)) {
				SetError(error, "write config file failed");
				return false;
			}

			SetError(error, std::string());
			return true;
		}

	} // namespace ServerConfigLicense
} // namespace AssetLib
