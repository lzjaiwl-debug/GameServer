#include "../AdminCertification.h"

namespace AdminCertification {

	static const char kAdminKeyMagic[4] = { 'A', 'K', 'F', '1' };

	int64 AdminUsbKeyIssuer::NowSec()
	{
		return Upp::GetUTCSeconds(Upp::GetSysTime());
	}

	void AdminUsbKeyIssuer::SetServerSecret(const String& secret)
	{
		server_secret_ = secret;
	}

	bool AdminUsbKeyIssuer::HasServerSecret() const
	{
		return !server_secret_.IsEmpty();
	}

	String AdminUsbKeyIssuer::NormalizeUsername(const String& s)
	{
		return Upp::ToLower(Upp::TrimBoth(s));
	}

	String AdminUsbKeyIssuer::NormalizeUsbSerial(const String& s)
	{
		return Upp::ToUpper(Upp::TrimBoth(s));
	}

	void AdminUsbKeyIssuer::PutU16(String& out, uint16 v)
	{
		out.Cat((char)((v >> 8) & 0xFF));
		out.Cat((char)(v & 0xFF));
	}

	void AdminUsbKeyIssuer::PutU32(String& out, uint32 v)
	{
		for (int i = 3; i >= 0; i--)
			out.Cat((char)((v >> (i * 8)) & 0xFF));
	}

	void AdminUsbKeyIssuer::PutU64(String& out, uint64 v)
	{
		for (int i = 7; i >= 0; i--)
			out.Cat((char)((v >> (i * 8)) & 0xFF));
	}

	bool AdminUsbKeyIssuer::GetU16(const String& in, int& off, uint16& v)
	{
		if (off + 2 > in.GetCount()) return false;
		const byte* p = (const byte*)in.Begin();
		v = (uint16)(((uint16)p[off] << 8) | (uint16)p[off + 1]);
		off += 2;
		return true;
	}

	bool AdminUsbKeyIssuer::GetU32(const String& in, int& off, uint32& v)
	{
		if (off + 4 > in.GetCount()) return false;
		const byte* p = (const byte*)in.Begin();
		v = ((uint32)p[off] << 24) | ((uint32)p[off + 1] << 16) | ((uint32)p[off + 2] << 8) | (uint32)p[off + 3];
		off += 4;
		return true;
	}

	bool AdminUsbKeyIssuer::GetU64(const String& in, int& off, uint64& v)
	{
		if (off + 8 > in.GetCount()) return false;
		const byte* p = (const byte*)in.Begin();
		v = 0;
		for (int i = 0; i < 8; i++)
			v = (v << 8) | (uint64)p[off + i];
		off += 8;
		return true;
	}

	bool AdminUsbKeyIssuer::RandomBytes(String& out, int len)
	{
		if (len <= 0)
			return false;
		std::vector<unsigned char> buf((size_t)len);
		if (RAND_bytes(buf.data(), len) != 1)
			return false;
		out = String((const char*)buf.data(), len);
		return true;
	}

	bool AdminUsbKeyIssuer::DeriveKey(const String& password, const String& salt, int iterations, String& out32)
	{
		if (password.IsEmpty() || salt.IsEmpty() || iterations <= 0)
			return false;
		std::vector<unsigned char> key(32);
		int ok = PKCS5_PBKDF2_HMAC(password.Begin(), password.GetCount(),
			(const unsigned char*)salt.Begin(), salt.GetCount(),
			iterations, EVP_sha256(), (int)key.size(), key.data());
		if (ok != 1)
			return false;
		out32 = String((const char*)key.data(), (int)key.size());
		return true;
	}

	bool AdminUsbKeyIssuer::EncryptAes256Gcm(const String& key32, const String& iv12, const String& plain, String& cipher, String& tag16)
	{
		if (key32.GetCount() != 32 || iv12.GetCount() != IV_LEN)
			return false;

		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (!ctx)
			return false;

		bool ok = false;
		do {
			if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) break;
			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv12.GetCount(), NULL) != 1) break;
			if (EVP_EncryptInit_ex(ctx, NULL, NULL,
				(const unsigned char*)key32.Begin(),
				(const unsigned char*)iv12.Begin()) != 1) break;

			std::vector<unsigned char> out((size_t)plain.GetCount() + 16);
			int outl = 0;
			if (EVP_EncryptUpdate(ctx, out.data(), &outl,
				(const unsigned char*)plain.Begin(), plain.GetCount()) != 1) break;
			int finl = 0;
			if (EVP_EncryptFinal_ex(ctx, out.data() + outl, &finl) != 1) break;
			outl += finl;

			unsigned char tag[TAG_LEN];
			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, TAG_LEN, tag) != 1) break;

			cipher = String((const char*)out.data(), outl);
			tag16 = String((const char*)tag, TAG_LEN);
			ok = true;
		} while (0);

		EVP_CIPHER_CTX_free(ctx);
		return ok;
	}

	bool AdminUsbKeyIssuer::DecryptAes256Gcm(const String& key32, const String& iv12, const String& cipher, const String& tag16, String& plain)
	{
		if (key32.GetCount() != 32 || iv12.GetCount() != IV_LEN || tag16.GetCount() != TAG_LEN)
			return false;

		EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
		if (!ctx)
			return false;

		bool ok = false;
		do {
			if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) break;
			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, iv12.GetCount(), NULL) != 1) break;
			if (EVP_DecryptInit_ex(ctx, NULL, NULL,
				(const unsigned char*)key32.Begin(),
				(const unsigned char*)iv12.Begin()) != 1) break;

			std::vector<unsigned char> out((size_t)cipher.GetCount() + 16);
			int outl = 0;
			if (EVP_DecryptUpdate(ctx, out.data(), &outl,
				(const unsigned char*)cipher.Begin(), cipher.GetCount()) != 1) break;
			if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, TAG_LEN, (void*)tag16.Begin()) != 1) break;
			int finl = 0;
			if (EVP_DecryptFinal_ex(ctx, out.data() + outl, &finl) != 1) break;
			outl += finl;

			plain = String((const char*)out.data(), outl);
			ok = true;
		} while (0);

		EVP_CIPHER_CTX_free(ctx);
		return ok;
	}

	bool AdminUsbKeyIssuer::HmacSha256(const String& key, const String& data, String& out32)
	{
		unsigned char mac[SIG_LEN];
		unsigned int maclen = 0;
		if (!HMAC(EVP_sha256(), key.Begin(), key.GetCount(),
			(const unsigned char*)data.Begin(), data.GetCount(), mac, &maclen))
			return false;
		if (maclen != (unsigned int)SIG_LEN)
			return false;
		out32 = String((const char*)mac, SIG_LEN);
		return true;
	}

	String AdminUsbKeyIssuer::BuildPlainPayload(const String& username_lc,
		const String& usb_serial,
		const String& device_secret,
		int64 issued_at,
		int64 expire_at,
		uint64 nonce)
	{
		String out;
		out.Cat((char)2);
		PutU16(out, (uint16)username_lc.GetCount());
		out.Cat(username_lc);
		PutU16(out, (uint16)usb_serial.GetCount());
		out.Cat(usb_serial);
		PutU16(out, (uint16)device_secret.GetCount());
		out.Cat(device_secret);
		PutU64(out, (uint64)issued_at);
		PutU64(out, (uint64)expire_at);
		PutU64(out, nonce);
		return out;
	}

	bool AdminUsbKeyIssuer::ParsePlainPayload(const String& plain, AdminKeyPayload& out)
	{
		int off = 0;
		if (plain.GetCount() < 1)
			return false;
		out.ver = (unsigned char)plain[off++];
		if (out.ver != 1 && out.ver != 2)
			return false;

		uint16 ulen = 0, slen = 0, dlen = 0;
		uint64 issue = 0, expire = 0, nonce = 0;
		if (!GetU16(plain, off, ulen)) return false;
		if (off + ulen > plain.GetCount()) return false;
		out.username_lc = plain.Mid(off, ulen);
		off += ulen;
		if (!GetU16(plain, off, slen)) return false;
		if (off + slen > plain.GetCount()) return false;
		out.usb_serial = plain.Mid(off, slen);
		off += slen;
		if (out.ver >= 2) {
			if (!GetU16(plain, off, dlen)) return false;
			if (off + dlen > plain.GetCount()) return false;
			out.device_secret = plain.Mid(off, dlen);
			off += dlen;
		}
		else {
			out.device_secret.Clear();
		}
		if (!GetU64(plain, off, issue)) return false;
		if (!GetU64(plain, off, expire)) return false;
		if (!GetU64(plain, off, nonce)) return false;
		if (off != plain.GetCount()) return false;
		out.issued_at = (int64)issue;
		out.expire_at = (int64)expire;
		out.nonce = nonce;
		return true;
	}

	String AdminUsbKeyIssuer::IssueKeyFile(const String& username,
		const String& password,
		const String& usb_serial,
		int64 ttl_sec,
		String* error,
		String* issued_device_secret) const
	{
		auto seterr = [&](const char* e) {
			if (error) *error = e;
			};

		if (!HasServerSecret()) { seterr("server secret not configured"); return String(); }
		String uname = NormalizeUsername(username);
		String usb = NormalizeUsbSerial(usb_serial);
		if (uname.IsEmpty()) { seterr("username is empty"); return String(); }
		if (usb.IsEmpty()) { seterr("usb serial is empty"); return String(); }
		if (password.IsEmpty()) { seterr("password is empty"); return String(); }
		if (uname.GetCount() > 65535 || usb.GetCount() > 65535) { seterr("username or usb serial too long"); return String(); }
		if (issued_device_secret)
			issued_device_secret->Clear();

		String salt;
		if (!RandomBytes(salt, SALT_LEN)) { seterr("random salt failed"); return String(); }
		String iv;
		if (!RandomBytes(iv, IV_LEN)) { seterr("random iv failed"); return String(); }
		String nonce_raw;
		if (!RandomBytes(nonce_raw, 8)) { seterr("random nonce failed"); return String(); }
		uint64 nonce = 0;
		for (int i = 0; i < 8; i++)
			nonce = (nonce << 8) | (uint64)(byte)nonce_raw[i];

		int64 issued_at = NowSec();
		if (ttl_sec <= 0)
			ttl_sec = 60;
		int64 expire_at = issued_at + ttl_sec;

		String key32;
		if (!DeriveKey(password, salt, DEFAULT_PBKDF2_ITER, key32)) { seterr("derive key failed"); return String(); }
		String device_secret_raw;
		if (!RandomBytes(device_secret_raw, 32)) { seterr("random device secret failed"); return String(); }
		String device_secret = Upp::Base64Encode(device_secret_raw);

		String plain = BuildPlainPayload(uname, usb, device_secret, issued_at, expire_at, nonce);
		String cipher, tag;
		if (!EncryptAes256Gcm(key32, iv, plain, cipher, tag)) { seterr("encrypt failed"); return String(); }

		String env;
		env.Cat(kAdminKeyMagic, 4);
		env.Cat((char)2);
		PutU32(env, (uint32)DEFAULT_PBKDF2_ITER);
		env.Cat((char)SALT_LEN);
		env.Cat((char)IV_LEN);
		env.Cat((char)TAG_LEN);
		PutU64(env, (uint64)issued_at);
		PutU64(env, (uint64)expire_at);
		PutU32(env, (uint32)cipher.GetCount());
		PutU16(env, (uint16)uname.GetCount());
		env.Cat(uname);
		env.Cat(salt);
		env.Cat(iv);
		env.Cat(tag);
		env.Cat(cipher);

		String sig;
		if (!HmacSha256(server_secret_, env, sig)) { seterr("hmac failed"); return String(); }
		String raw = env;
		raw.Cat(sig);
		if (issued_device_secret)
			*issued_device_secret = device_secret;
		seterr("");
		return Upp::Base64Encode(raw);
	}

	bool AdminUsbKeyIssuer::VerifyKeyFile(const String& keyfile,
		const String& username,
		const String& password,
		const String& usb_serial,
		AdminKeyPayload* out,
		String* error,
		String* out_device_secret) const
	{
		auto seterr = [&](const char* e) {
			if (error) *error = e;
			};

		if (!HasServerSecret()) { seterr("server secret not configured"); return false; }
		if (keyfile.IsEmpty()) { seterr("keyfile is empty"); return false; }

		String raw = keyfile;
		if (!(raw.GetCount() >= 4 && memcmp(raw.Begin(), kAdminKeyMagic, 4) == 0))
			raw = Upp::Base64Decode(keyfile);
		if (raw.GetCount() < 4 + 1 + 4 + 1 + 1 + 1 + 8 + 8 + 4 + 2 + SIG_LEN) {
			seterr("keyfile length invalid");
			return false;
		}

		int off = 0;
		if (memcmp(raw.Begin() + off, kAdminKeyMagic, 4) != 0) { seterr("keyfile magic invalid"); return false; }
		off += 4;

		int ver = (unsigned char)raw[off++];
		if (ver != 1 && ver != 2) { seterr("keyfile version invalid"); return false; }

		uint32 iterations = 0, cipher_len = 0;
		uint64 issue = 0, expire = 0;
		uint16 user_len = 0;
		if (!GetU32(raw, off, iterations)) { seterr("parse iterations failed"); return false; }
		if (off + 3 > raw.GetCount()) { seterr("parse fixed lens failed"); return false; }
		int salt_len = (unsigned char)raw[off++];
		int iv_len = (unsigned char)raw[off++];
		int tag_len = (unsigned char)raw[off++];
		if (!GetU64(raw, off, issue) || !GetU64(raw, off, expire)) { seterr("parse issue/expire failed"); return false; }
		if (!GetU32(raw, off, cipher_len) || !GetU16(raw, off, user_len)) { seterr("parse lengths failed"); return false; }

		int env_min = off + user_len + salt_len + iv_len + tag_len + (int)cipher_len;
		if (env_min + SIG_LEN != raw.GetCount()) {
			seterr("keyfile body length mismatch");
			return false;
		}
		if (iterations <= 0 || salt_len <= 0 || iv_len <= 0 || tag_len != TAG_LEN) {
			seterr("keyfile crypto params invalid");
			return false;
		}

		String env = raw.Mid(0, raw.GetCount() - SIG_LEN);
		String sig = raw.Mid(raw.GetCount() - SIG_LEN, SIG_LEN);
		String calc;
		if (!HmacSha256(server_secret_, env, calc)) { seterr("hmac failed"); return false; }
		if (CRYPTO_memcmp(calc.Begin(), sig.Begin(), SIG_LEN) != 0) {
			seterr("keyfile signature mismatch");
			return false;
		}

		String username_in_file = raw.Mid(off, user_len);
		off += user_len;
		String salt = raw.Mid(off, salt_len);
		off += salt_len;
		String iv = raw.Mid(off, iv_len);
		off += iv_len;
		String tag = raw.Mid(off, tag_len);
		off += tag_len;
		String cipher = raw.Mid(off, (int)cipher_len);
		off += (int)cipher_len;

		if (NowSec() > (int64)expire) {
			seterr("keyfile expired");
			return false;
		}

		String uname = NormalizeUsername(username);
		String usb = NormalizeUsbSerial(usb_serial);
		if (uname.IsEmpty() || usb.IsEmpty() || password.IsEmpty()) {
			seterr("verify input empty");
			return false;
		}
		if (uname != username_in_file) {
			seterr("username mismatch");
			return false;
		}
		if (out_device_secret)
			out_device_secret->Clear();

		String key32;
		if (!DeriveKey(password, salt, (int)iterations, key32)) { seterr("derive key failed"); return false; }

		String plain;
		if (!DecryptAes256Gcm(key32, iv, cipher, tag, plain)) {
			seterr("decrypt failed");
			return false;
		}

		AdminKeyPayload payload;
		if (!ParsePlainPayload(plain, payload)) {
			seterr("payload parse failed");
			return false;
		}
		if (payload.username_lc != uname) {
			seterr("payload username mismatch");
			return false;
		}
		if (payload.usb_serial != usb) {
			seterr("usb serial mismatch");
			return false;
		}
		if (payload.expire_at != (int64)expire || payload.issued_at != (int64)issue) {
			seterr("payload time mismatch");
			return false;
		}

		if (out)
			*out = payload;
		if (out_device_secret)
			*out_device_secret = payload.device_secret;
		seterr("");
		return true;
	}

#ifdef _WIN32
	bool AdminUsbKeyIssuer::ReadDeviceSerialByRoot(const String& root, String& serial)
	{
		{
			serial.Clear();
			if (root.GetCount() < 2)
				return false;
			String dev = "\\\\.\\";
			dev.Cat(root[0]);
			dev.Cat(":");

			HANDLE h = CreateFileA(~dev, 0,
				FILE_SHARE_READ | FILE_SHARE_WRITE,
				NULL, OPEN_EXISTING, 0, NULL);
			if (h == INVALID_HANDLE_VALUE)
				return false;

			STORAGE_PROPERTY_QUERY query;
			ZeroMemory(&query, sizeof(query));
			query.PropertyId = StorageDeviceProperty;
			query.QueryType = PropertyStandardQuery;

			BYTE outbuf[1024];
			DWORD ret = 0;
			BOOL ok = DeviceIoControl(h, IOCTL_STORAGE_QUERY_PROPERTY,
				&query, sizeof(query), outbuf, sizeof(outbuf), &ret, NULL);
			CloseHandle(h);
			if (!ok || ret < sizeof(STORAGE_DEVICE_DESCRIPTOR))
				return false;

			const STORAGE_DEVICE_DESCRIPTOR* desc = (const STORAGE_DEVICE_DESCRIPTOR*)outbuf;
			if (desc->SerialNumberOffset == 0 || desc->SerialNumberOffset >= ret)
				return false;

			const char* p = (const char*)(outbuf + desc->SerialNumberOffset);
			String s = TrimBoth(String(p));
			if (s.IsEmpty())
				return false;
			serial = ToUpper(s);
			return true;
		}
	}
#elif defined(PLATFORM_POSIX)
	String AdminUsbKeyIssuer::ReadTextFileTrim(const String & path)
	{
		std::ifstream ifs(~path);
		if (!ifs.is_open())
			return String();
		std::string s;
		std::getline(ifs, s);
		return TrimBoth(String(s.c_str()));
	}

	String AdminUsbKeyIssuer::BlockBaseNameFromDevice(const String & devpath)
	{
		if (!devpath.StartsWith("/dev/"))
			return String();
		String n = devpath.Mid(5);
		if (n.StartsWith("nvme") || n.StartsWith("mmcblk")) {
			int p = n.ReverseFind('p');
			if (p > 0) {
				bool all_digit = true;
				for (int i = p + 1; i < n.GetCount(); ++i) {
					if (!IsDigit((byte)n[i])) { all_digit = false; break; }
				}
				if (all_digit)
					n = n.Left(p);
			}
		}
		else {
			while (!n.IsEmpty() && IsDigit((byte)n[n.GetCount() - 1]))
				n.Trim(n.GetCount() - 1);
		}
		return n;
	}

	bool AdminUsbKeyIssuer::IsRemovableOrUsbBlock(const String & base)
	{
		if (base.IsEmpty())
			return false;
		String removable = ReadTextFileTrim("/sys/class/block/" + base + "/removable");
		if (removable == "1")
			return true;
		String uevent = ReadTextFileTrim("/sys/class/block/" + base + "/device/uevent");
		if (uevent.Find("DRIVER=usb-storage") >= 0 || uevent.Find("DRIVER=uas") >= 0)
			return true;
		String subsys = ReadTextFileTrim("/sys/class/block/" + base + "/device/subsystem/uevent");
		return subsys.Find("USB") >= 0;
	}

	String AdminUsbKeyIssuer::ReadLinuxDeviceSerial(const String & base, const String & devpath)
	{
		String s = ReadTextFileTrim("/sys/class/block/" + base + "/device/serial");
		if (!s.IsEmpty())
			return ToUpper(s);
		s = ReadTextFileTrim("/sys/block/" + base + "/device/serial");
		if (!s.IsEmpty())
			return ToUpper(s);

		String query_dev = devpath;
		if (query_dev.IsEmpty())
			query_dev = "/dev/" + base;
		String cmd = "udevadm info --query=property --name=" + query_dev + " 2>/dev/null";
		FILE* fp = popen(~cmd, "r");
		if (!fp)
			return String();

		String serial;
		char line[512];
		while (fgets(line, sizeof(line), fp)) {
			String ln = TrimBoth(String(line));
			if (ln.StartsWith("ID_SERIAL_SHORT=")) {
				serial = ln.Mid((int)strlen("ID_SERIAL_SHORT="));
				break;
			}
			if (serial.IsEmpty() && ln.StartsWith("ID_SERIAL="))
				serial = ln.Mid((int)strlen("ID_SERIAL="));
		}
		pclose(fp);
		return serial.IsEmpty() ? String() : ToUpper(TrimBoth(serial));
	}
#endif
		Vector<RemovableDriveInfo> AdminUsbKeyIssuer::EnumerateRemovableDrives()
		{
#ifdef _WIN32
			Vector<RemovableDriveInfo> out;
			DWORD mask = GetLogicalDrives();
			for (int i = 0; i < 26; ++i) {
				if (((mask >> i) & 1) == 0)
					continue;
				char letter = (char)('A' + i);
				char rootA[4] = { letter, ':', '\\', 0 };
				if (GetDriveTypeA(rootA) != DRIVE_REMOVABLE)
					continue;

				String root(rootA);
				String serial;
				if (!ReadDeviceSerialByRoot(root, serial))
					serial.Clear();
				out.Add({ root, serial });
			}
			return out;
#elif defined(PLATFORM_POSIX)
			Vector<RemovableDriveInfo> out;
			std::ifstream ifs("/proc/mounts");
			if (!ifs.is_open())
				return out;

			std::set<std::string> seen_mount;
			std::string line;
			while (std::getline(ifs, line)) {
				if (line.empty()) continue;
				std::istringstream iss(line);
				std::string dev, mnt, fstype;
				if (!(iss >> dev >> mnt >> fstype))
					continue;
				if (dev.rfind("/dev/", 0) != 0)
					continue;
				if (seen_mount.find(mnt) != seen_mount.end())
					continue;

				String base = BlockBaseNameFromDevice(dev.c_str());
				if (base.IsEmpty())
					continue;
				if (!IsRemovableOrUsbBlock(base))
					continue;

				RemovableDriveInfo info;
				info.root = mnt.c_str();
				info.device_serial = ReadLinuxDeviceSerial(base);
				out.Add(info);
				seen_mount.insert(mnt);
			}
			return out;
#endif
		}
}
