#include "../AccountServer.h"

// ============================================================================
// 实现说明
// ----------------------------------------------------------------------------
// 1. 所有 KDF 参数通过 OSSL_PARAM 传递，而 OSSL_PARAM_construct_* 只保存指针
//    不拷贝数据。因此下面的参数变量必须活到 EVP_KDF_derive 返回之后。
// 2. U++ 的 Random() / Random64() 基于普通 PRNG，不是密码学安全的，
//    生成盐一律走 OpenSSL RAND_bytes。
// 3. 比较一律走 CRYPTO_memcmp，绝不用 memcmp（后者短路返回，存在时序泄露）。
// ============================================================================

// ---------------------------------------------------------------------------
// 内部小工具
// ---------------------------------------------------------------------------

// 按分隔符切分（不用 U++ Split，避免版本差异）
static void SplitBy(const String& s, char sep, Vector<String>& out)
{
	out.Clear();
	const char* p = s.Begin();
	int n = s.GetCount();
	int start = 0;
	for (int i = 0; i < n; i++) {
		if (p[i] == sep) {
			out.Add(String(p + start, i - start));
			start = i + 1;
		}
	}
	out.Add(String(p + start, n - start));
}

// 从 "m=19456,t=2,p=1" 形式的串里取整数参数
static int ParamInt(const String& list, const char* key, int def)
{
	const char* p = list.Begin();
	int   n    = list.GetCount();
	int   klen = (int)strlen(key);
	int   i    = 0;
	while (i < n) {
		int j = i;
		while (j < n && p[j] != ',') j++;
		int toklen = j - i;
		if (toklen > klen && memcmp(p + i, key, (size_t)klen) == 0 && p[i + klen] == '=') {
			String v(p + i + klen + 1, toklen - klen - 1);
			return atoi(~v);
		}
		i = j + 1;
	}
	return def;
}

static bool SameAlgo(const String& a, const char* b)
{
	return a.GetCount() == (int)strlen(b) && memcmp(a.Begin(), b, (size_t)a.GetCount()) == 0;
}

// ---------------------------------------------------------------------------
// 随机盐
// ---------------------------------------------------------------------------
bool PasswordHasher::RandomSalt(String& out, int len)
{
	std::vector<unsigned char> buf((size_t)len);
	if (RAND_bytes(buf.data(), len) != 1)
		return false;
	out = String((const char*)buf.data(), len);
	return true;
}

// ---------------------------------------------------------------------------
// Argon2id 派生
// ---------------------------------------------------------------------------
bool PasswordHasher::DeriveArgon2(const String& password, const String& salt,
                                  const Argon2Params& ap, String& out_raw)
{
	EVP_KDF* kdf = EVP_KDF_fetch(NULL, "ARGON2ID", NULL);
	if (!kdf)
		kdf = EVP_KDF_fetch(NULL, "argon2id", NULL);   // 大小写兼容
	if (!kdf)
		return false;

	EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
	EVP_KDF_free(kdf);                                 // ctx 已持有引用
	if (!ctx)
		return false;

	// ★ 这些变量必须在 derive 期间保持存活：OSSL_PARAM 只引用不拷贝
	unsigned int u_mem  = (unsigned int)ap.memcost_kib;
	unsigned int u_iter = (unsigned int)ap.iterations;
	unsigned int u_lane = (unsigned int)ap.lanes;
	unsigned int u_ver  = (unsigned int)ap.version;
	unsigned int u_size = (unsigned int)ap.outlen;

	OSSL_PARAM params[8];
	int n = 0;
	params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD,
	                  (void*)password.Begin(), (size_t)password.GetCount());
	params[n++] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT,
	                  (void*)salt.Begin(), (size_t)salt.GetCount());
	params[n++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ITER,           &u_iter);
	params[n++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_MEMCOST, &u_mem);
	params[n++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_LANES,   &u_lane);
	params[n++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_ARGON2_VERSION, &u_ver);
	params[n++] = OSSL_PARAM_construct_uint32(OSSL_KDF_PARAM_SIZE,           &u_size);
	params[n++] = OSSL_PARAM_construct_end();

	bool ok = false;
	if (EVP_KDF_CTX_set_params(ctx, params) > 0 && ap.outlen > 0) {
		std::vector<unsigned char> buf(ap.outlen);
		if (EVP_KDF_derive(ctx, buf.data(), buf.size(), NULL) > 0) {
			out_raw = String((const char*)buf.data(), (int)buf.size());
			ok = true;
		}
	}

	EVP_KDF_CTX_free(ctx);
	return ok;
}

// ---------------------------------------------------------------------------
// PBKDF2-HMAC-SHA256 派生（兼容通道 / 跨语言对拍用）
// ---------------------------------------------------------------------------
bool PasswordHasher::DerivePbkdf2(const String& password, const String& salt,
                                  const Pbkdf2Params& pp, String& out_raw)
{
	if (password.GetCount() <= 0 || salt.GetCount() <= 0 || pp.outlen <= 0)
		return false;

	std::vector<unsigned char> buf(pp.outlen);
	int r = PKCS5_PBKDF2_HMAC(password.Begin(), password.GetCount(),
	                          (const unsigned char*)salt.Begin(), salt.GetCount(),
	                          (int)pp.iterations, EVP_sha256(),
	                          (int)pp.outlen, buf.data());
	if (r != 1)
		return false;

	out_raw = String((const char*)buf.data(), (int)buf.size());
	return true;
}

// ---------------------------------------------------------------------------
// PHC 串构造
// ---------------------------------------------------------------------------
HashResult PasswordHasher::HashArgon2(const String& password, const String& salt,
                                      const Argon2Params& ap)
{
	HashResult r;
	String raw;
	if (!DeriveArgon2(password, salt, ap, raw)) {
		r.error = "argon2id derive failed";
		return r;
	}
	r.phc = Format("$argon2id$v=%d$m=%d,t=%d,p=%d$%s$%s",
	               (int)ap.version, (int)ap.memcost_kib, (int)ap.iterations, (int)ap.lanes,
	               ~Base64Encode(salt), ~Base64Encode(raw));
	r.algo  = "argon2id";
	r.ok    = true;
	return r;
}

HashResult PasswordHasher::HashPbkdf2Raw(const String& password, const String& salt,
                                         const Pbkdf2Params& pp)
{
	HashResult r;
	String raw;
	if (!DerivePbkdf2(password, salt, pp, raw)) {
		r.error = "pbkdf2 derive failed";
		return r;
	}
	r.phc = Format("$pbkdf2-sha256$i=%d,l=%d$%s$%s",
	               (int)pp.iterations, (int)pp.outlen,
	               ~Base64Encode(salt), ~Base64Encode(raw));
	r.algo  = "pbkdf2-sha256";
	r.ok    = true;
	return r;
}

// ---------------------------------------------------------------------------
// 对外：生成
// ---------------------------------------------------------------------------
HashResult PasswordHasher::Hash(const String& password)
{
	Argon2Params ap;
	return Hash(password, ap);
}

HashResult PasswordHasher::Hash(const String& password, const Argon2Params& ap)
{
	HashResult r;
	if (password.GetCount() < 8) { r.error = "password too short"; return r; }
	if (password.GetCount() > 128) { r.error = "password too long"; return r; }

	String salt;
	if (!RandomSalt(salt, SALT_LEN)) { r.error = "RAND_bytes failed"; return r; }
	return HashArgon2(password, salt, ap);
}

HashResult PasswordHasher::HashPbkdf2(const String& password)
{
	Pbkdf2Params pp;
	return HashPbkdf2(password, pp);
}

HashResult PasswordHasher::HashPbkdf2(const String& password, const Pbkdf2Params& pp)
{
	HashResult r;
	if (password.GetCount() < 8) { r.error = "password too short"; return r; }
	if (password.GetCount() > 128) { r.error = "password too long"; return r; }

	String salt;
	if (!RandomSalt(salt, SALT_LEN)) { r.error = "RAND_bytes failed"; return r; }
	return HashPbkdf2Raw(password, salt, pp);
}

// ---------------------------------------------------------------------------
// 对外：验证
// ---------------------------------------------------------------------------
bool PasswordHasher::Verify(const String& password, const String& phc, bool* need_upgrade)
{
	if (need_upgrade)
		*need_upgrade = false;
	if (password.GetCount() <= 0 || password.GetCount() > 128 || phc.IsEmpty())
		return false;

	Vector<String> seg;
	SplitBy(phc, '$', seg);
	// argon2id: ["", "argon2id", "v=19", "m=..,t=..,p=..", salt, hash]  -> 6 段
	// pbkdf2  : ["", "pbkdf2-sha256", "i=..,l=..", salt, hash]          -> 5 段

	String cand;
	bool   derived = false;

	if (seg.GetCount() == 6 && SameAlgo(seg[1], "argon2id")) {
		Argon2Params ap;
		ap.version     = (uint32_t)ParamInt(seg[2], "v", 0x13);
		ap.memcost_kib = (uint32_t)ParamInt(seg[3], "m", 0);
		ap.iterations  = (uint32_t)ParamInt(seg[3], "t", 0);
		ap.lanes       = (uint32_t)ParamInt(seg[3], "p", 0);
		String salt = Base64Decode(seg[4]);
		String want = Base64Decode(seg[5]);
		ap.outlen = (size_t)want.GetCount() > 0 ? (size_t)want.GetCount() : 32;
		if (ap.memcost_kib == 0 || ap.iterations == 0 || ap.lanes == 0)
			return false;
		derived = DeriveArgon2(password, salt, ap, cand);
		if (derived && CRYPTO_memcmp(cand.Begin(), want.Begin(), (size_t)want.GetCount()) != 0)
			return false;
	}
	else if (seg.GetCount() == 5 && SameAlgo(seg[1], "pbkdf2-sha256")) {
		Pbkdf2Params pp;
		pp.iterations = (uint32_t)ParamInt(seg[2], "i", 0);
		String salt = Base64Decode(seg[3]);
		String want = Base64Decode(seg[4]);
		pp.outlen = (size_t)want.GetCount() > 0 ? (size_t)want.GetCount() : 32;
		if (pp.iterations == 0)
			return false;
		derived = DerivePbkdf2(password, salt, pp, cand);
		if (derived && CRYPTO_memcmp(cand.Begin(), want.Begin(), (size_t)want.GetCount()) != 0)
			return false;
	}
	else {
		return false;   // 未知格式
	}

	if (!derived)
		return false;

	if (need_upgrade)
		*need_upgrade = NeedUpgrade(phc);
	return true;
}

// ---------------------------------------------------------------------------
// 是否需要静默升级
// ---------------------------------------------------------------------------
bool PasswordHasher::NeedUpgrade(const String& phc)
{
	Vector<String> seg;
	SplitBy(phc, '$', seg);
	if (seg.GetCount() < 5)
		return true;
	if (!SameAlgo(seg[1], "argon2id"))
		return true;                       // 非 argon2id 一律升级

	Argon2Params def;
	int m = ParamInt(seg[3], "m", 0);
	int t = ParamInt(seg[3], "t", 0);
	int p = ParamInt(seg[3], "p", 0);
	return m < (int)def.memcost_kib || t < (int)def.iterations || p < (int)def.lanes;
}

String PasswordHasher::AlgoOf(const String& phc)
{
	Vector<String> seg;
	SplitBy(phc, '$', seg);
	return seg.GetCount() >= 2 ? seg[1] : String();
}

// ---------------------------------------------------------------------------
// 自洽性自检
// ---------------------------------------------------------------------------
bool PasswordHasher::SelfTest()
{
	String pw = "Wang#12345";

	HashResult h = Hash(pw);
	if (!h.ok) return false;
	if (!Verify(pw, h.phc)) return false;
	if (Verify("Wang#12346", h.phc)) return false;

	HashResult h2 = Hash(pw);
	if (!h2.ok) return false;
	if (h.phc == h2.phc) return false;          // 两次哈希的盐必须不同
	if (!Verify(pw, h2.phc)) return false;

	Pbkdf2Params fast;                          // 自检用低迭代，避免拖慢启动
	fast.iterations = 1000;
	HashResult p1 = HashPbkdf2(pw, fast);
	if (!p1.ok) return false;
	if (!Verify(pw, p1.phc)) return false;
	if (Verify("Wang#12346", p1.phc)) return false;
	if (!NeedUpgrade(p1.phc)) return false;     // pbkdf2 应判定需升级
	if (NeedUpgrade(h.phc)) return false;       // 新参数 argon2id 不需升级

	if (!SameAlgo(AlgoOf(h.phc), "argon2id")) return false;

	String s1, s2;
	if (!RandomSalt(s1) || !RandomSalt(s2)) return false;
	if (s1 == s2) return false;                 // 随机盐不可重复

	return true;
}

// ============================================================================
// 线程化封装实现
// ----------------------------------------------------------------------------
// 说明：U++ 的 String 带引用计数且不可原地改写，无法安全地对密码缓冲区做
// OPENSSL_cleanse。若你对"明文不留在内存"有强要求，请把密码改用
// Vector<byte> 传递，并在用完后 sodium_memzero / OPENSSL_cleanse。
// ============================================================================
CredentialWorker& CredentialWorker::Instance()
{
	static CredentialWorker w;
	return w;
}

void CredentialWorker::Start(int threads)
{
	Stop();
	if (threads < 1) threads = 1;
	{
		std::lock_guard<std::mutex> lk(mtx);
		running = true;
	}
	for (int i = 0; i < threads; i++)
		workers.emplace_back([this] { WorkerLoop(); });
}

void CredentialWorker::Stop()
{
	{
		std::lock_guard<std::mutex> lk(mtx);
		if (!running && workers.empty())
			return;
		running = false;
	}
	cv.notify_all();
	for (size_t i = 0; i < workers.size(); i++)
		if (workers[i].joinable())
			workers[i].join();
	workers.clear();
}

void CredentialWorker::AsyncVerify(String password, String phc, VerifyCb cb)
{
	Job j;
	j.op       = OP_VERIFY;
	j.password = password;
	j.phc      = phc;
	j.vcb      = cb;
	{
		std::lock_guard<std::mutex> lk(mtx);
		queue.push_back(j);
	}
	cv.notify_one();
}

void CredentialWorker::AsyncHash(String password, HashCb cb)
{
	Job j;
	j.op       = OP_HASH;
	j.password = password;
	j.ap       = Argon2Params();
	j.hcb      = cb;
	{
		std::lock_guard<std::mutex> lk(mtx);
		queue.push_back(j);
	}
	cv.notify_one();
}

void CredentialWorker::WorkerLoop()
{
	for (;;) {
		Job job;
		{
			std::unique_lock<std::mutex> lk(mtx);
			cv.wait(lk, [this] { return !running || !queue.empty(); });
			if (!running && queue.empty())
				return;
			job = queue.front();
			queue.erase(queue.begin());
		}

		if (job.op == OP_VERIFY) {
			bool upgrade = false;
			bool ok = PasswordHasher::Verify(job.password, job.phc, &upgrade);
			if (job.vcb)
				job.vcb(ok, upgrade);
		}
		else {
			HashResult r = PasswordHasher::Hash(job.password, job.ap);
			if (job.hcb)
				job.hcb(r);
		}
	}
}
