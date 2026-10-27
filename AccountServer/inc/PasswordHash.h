#pragma once
// ============================================================================
// PasswordHash.h —— 账号凭据哈希（account_credential / admin_user 共用）
//
// 依赖：U++ Core（String / Base64Encode） + OpenSSL 3.x（EVP_KDF / RAND_bytes）
//
// 存储格式：PHC 自包含字符串，整串写进 account_credential.cred_hash
//   $argon2id$v=19$m=19456,t=2,p=1$<salt_b64>$<hash_b64>
//   $pbkdf2-sha256$i=600000,l=32$<salt_b64>$<hash_b64>
//   algo 字段冗余存 "argon2id" / "pbkdf2-sha256"，用于统计与迁移判断
//
// ★ 三个不可违反的约束：
//   1. 盐必须用 RAND_bytes（CSPRNG），禁用 U++ 的 Random()，它不是密码学安全的
//   2. 比较必须用 CRYPTO_memcmp（常数时间），禁用 memcmp
//   3. 派生耗时约 100~300ms，禁止在 AccountServer 单逻辑线程内调用，见文末说明
// ============================================================================

// ---------------------------------------------------------------------------
// 算法参数
// ---------------------------------------------------------------------------
struct Argon2Params
{
	uint32_t memcost_kib = 19456;   // m：内存开销，单位 KiB。19 MiB = OWASP 基线，实测后可上调到 65536
	uint32_t iterations  = 2;       // t：迭代轮数
	uint32_t lanes       = 1;       // p：并行度
	uint32_t version     = 0x13;    // Argon2 版本 19
	size_t   outlen      = 32;      // 输出字节数
};

struct Pbkdf2Params
{
	uint32_t iterations = 600000;   // OWASP 2023 对 HMAC-SHA256 的建议值
	size_t   outlen     = 32;
};

// ---------------------------------------------------------------------------
// 哈希结果
// ---------------------------------------------------------------------------
struct HashResult
{
	bool   ok    = false;
	String phc;                     // 成功时为完整 PHC 串，直接写库
	String algo;                    // 冗余算法名，写 account_credential.algo
	String error;                   // 失败原因，可写日志（绝不包含密码）
};

// ---------------------------------------------------------------------------
// 密码哈希器（无状态，线程安全，可在工作线程并发调用）
// ---------------------------------------------------------------------------
class PasswordHasher
{
public:
	static const int SALT_LEN = 16;

	// 生成哈希。内部自行产生随机盐。返回的 phc 整串写库
	static HashResult Hash(const String& password);
	static HashResult Hash(const String& password, const Argon2Params& ap);
	static HashResult HashPbkdf2(const String& password);
	static HashResult HashPbkdf2(const String& password, const Pbkdf2Params& pp);

	// 验证。phc 从库里读出来原样传入。
	// 返回 true 表示密码正确；同时通过 need_upgrade 告知是否该静默升级
	static bool Verify(const String& password, const String& phc, bool* need_upgrade = nullptr);

	// 判断库里的 phc 是否用了过期参数（登录成功后若为真，立即用新参数重新派生并 UPDATE）
	static bool NeedUpgrade(const String& phc);

	// 从 phc 中解析算法名（写库或统计用）
	static String AlgoOf(const String& phc);

	// 密码学安全随机盐
	static bool RandomSalt(String& out, int len = SALT_LEN);

	// 自洽性自检：哈希→验证→错误密码→不同盐，全部通过返回 true
	static bool SelfTest();

private:
	static HashResult HashArgon2(const String& password, const String& salt, const Argon2Params& ap);
	static HashResult HashPbkdf2Raw(const String& password, const String& salt, const Pbkdf2Params& pp);
	static bool       DeriveArgon2(const String& password, const String& salt,
	                               const Argon2Params& ap, String& out_raw);
	static bool       DerivePbkdf2(const String& password, const String& salt,
	                               const Pbkdf2Params& pp, String& out_raw);
};

// ============================================================================
// 线程化封装
// ----------------------------------------------------------------------------
// Argon2id 单次派生约 100~300ms，绝不能在 AccountServer 的单逻辑线程内执行，
// 否则一个玩家登录就会卡住全服所有请求。
//
// 用法（逻辑线程内）：
//   CredentialWorker::Instance().AsyncVerify(password, phc_from_db,
//       [](bool ok, bool need_upgrade) { ...回到逻辑线程继续处理... });
//
// 实现用 std::thread，是因为 U++ 的 Thread 对象一次 Run 后不能复用，
// 而这里需要常驻 worker + 条件变量语义。若你更愿意用 U++ CoWork，
// 可以把 WorkerLoop 里的任务体换成 CoWork::Schedule。
// ============================================================================
class CredentialWorker
{
public:
	enum Op { OP_VERIFY, OP_HASH };

	using VerifyCb = Function<void (bool ok, bool need_upgrade)>;
	using HashCb    = Function<void (HashResult r)>;

	static CredentialWorker& Instance();

	// 启动 n 个工作线程（建议 = CPU 物理核数的一半，最少 2）
	void Start(int threads = 2);
	void Stop();

	// 异步验证。回调在后台线程触发，调用方需自行把结果投递回逻辑线程
	void AsyncVerify(String password, String phc, VerifyCb cb);

	// 异步生成哈希
	void AsyncHash(String password, HashCb cb);

private:
	struct Job
	{
		Op          op       = OP_VERIFY;
		String      password;
		String      phc;
		Argon2Params ap;
		VerifyCb    vcb;
		HashCb      hcb;
	};

	CredentialWorker() {}
	CredentialWorker(const CredentialWorker&) = delete;
	CredentialWorker& operator=(const CredentialWorker&) = delete;

	void WorkerLoop();

	std::mutex               mtx;
	std::condition_variable  cv;
	std::vector<Job>         queue;
	std::vector<std::thread> workers;
	bool                     running = false;
};
