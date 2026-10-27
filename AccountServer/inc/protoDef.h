#pragma once
namespace AccountServer {
	struct EVT_BASE
	{
		inline static const char* REQUEST = "listen:request";//请求事件
		inline static const char* RESPONSE = "send:response";//响应事件
	};
	
	/// <summary>
	/// 响应基类
	/// </summary>
	struct RES_BASE : Moveable<RES_BASE>
	{
		std::string ev;//事件类型
		bool success;//是否成功	
		uint32_t code = 0;//错误码，0表示成功，非0表示失败
		std::string message;//错误信息，成功时为空
		RES_BASE() = default;
		virtual ~RES_BASE() = default;
		void set(const char* evt, bool issuccess, uint32_t c, const char* msg)
		{
			ev = evt;
			success = issuccess;
			code = c;
			message = msg ? msg : "";
		}
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s% ev% success% code% message;
		}
	};
	/// <summary>
	/// 请求基类
	/// </summary>
	struct REQ_BASE {
		std::string ev;//事件类型
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s% ev;
		}
	};
	/// <summary>
	/// 队列基类
	/// </summary>
	struct QUEUE_BASE : Moveable<QUEUE_BASE>
	{
		Event<Value> callback;//回调函数
		std::string ev;//事件类型
		AssetLib::BinaryData payload = nullptr;//原始请求帧
		virtual ~QUEUE_BASE() = default;
		QUEUE_BASE() = default;
		QUEUE_BASE(Event<Value> cb) : callback(cb) {}
		QUEUE_BASE(const AssetLib::BinaryData& p) : payload(p) {}
		QUEUE_BASE(const AssetLib::BinaryData& p, Event<Value> cb) : payload(p), callback(cb) {}

	};

	struct TIMETASK_BASE : Moveable<TIMETASK_BASE>
	{
		Event<Value> callback;//回调函数
		
		virtual ~TIMETASK_BASE() = default;
		TIMETASK_BASE() = default;
		TIMETASK_BASE(Event<Value> cb) : callback(cb) {}
	};
	/// <summary>
	/// 用户信息结构体
	/// </summary>
	struct USER_INFO : Moveable<USER_INFO>
	{
		NetLibrary::Clientinfo clientinfo;
		virtual ~USER_INFO() = default;
		USER_INFO() = default;
		USER_INFO(const NetLibrary::Clientinfo& ci)
			: clientinfo(ci) {
		}
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s% clientinfo;
		}
	};

	namespace mysqlProto
	{
		enum class QueryType : uint32_t
		{
			QUERYRET = 0,// 查询返回结果集
			QUERYNULL = 1,// 查询不返回结果集
			STORED = 2,// 调用存储过程
		};
		enum class MysqlResult : uint32_t
		{
			OK = 0,
			QUERY_ERROR = 1,    // 查询错误
			CONNECTION_ERROR = 2,    // 数据库连接错误
			SYSTEM_ERROR = 99,    // 系统错误
		};
		struct QeueTask : public QUEUE_BASE
		{
			QueryType query_type;
			QeueTask() = default;
			QeueTask(Event<Value> cb) : QUEUE_BASE(cb) {}
		};
		struct QueryTask : public QeueTask
		{
			std::string sql;
			Vector<Value> params;
			QueryTask() = default;
			QueryTask(QueryType qt, const std::string& s, const Vector<Value>& p, Event<Value>& cb)
				: QeueTask(cb), sql(s) {
				query_type = qt;
				params.Append(p);
			}
			QueryTask(QueryType qt, const std::string& s, const Vector<Value>& p, Event<Value>&& cb)
				: QeueTask(pick(cb)), sql(s) {
				query_type = qt;
				params.Append(p);
			}
			QueryTask(QueryType qt, const std::string& s, const Vector<Value>& p, NetLibrary::EMIEVENT cb, const AssetLib::BinaryData& d)
				: sql(s) {
				query_type = qt;
				params.Append(p);
				ev = cb;
				payload = d;
			}
			QueryTask(QueryType qt, const std::string& s, const Vector<Value>& p, NetLibrary::EMIEVENT cb) : sql(s) {
				query_type = qt;
				params.Append(p);
				ev = cb;
			}
		};
		struct QueryStoredTask : public QeueTask
		{
			std::string proc;
			Vector<Value> in_params;
			Vector<String> out_params;
			QueryStoredTask() = default;
			QueryStoredTask(const std::string& p, const Vector<Value>& in, const Vector<String>& out, Event<Value>& cb)
				: QeueTask(cb), proc(p) {
				query_type = QueryType::STORED;
				in_params.Append(in);
				out_params.Append(out);
			}
			QueryStoredTask(const std::string& p, const Vector<Value>& in, const Vector<String>& out, Event<Value>&& cb)
				: QeueTask(pick(cb)), proc(p) {
				query_type = QueryType::STORED;
				in_params.Append(in);
				out_params.Append(out);
			}
			QueryStoredTask(const std::string& p, const Vector<Value>& in, const Vector<String>& out, NetLibrary::EMIEVENT cb, const AssetLib::BinaryData& d)
				: proc(p) {
				query_type = QueryType::STORED;
				in_params.Append(in);
				out_params.Append(out);
				ev = cb;
				payload = d;
			}
			QueryStoredTask(const std::string& p, const Vector<Value>& in, const Vector<String>& out, NetLibrary::EMIEVENT cb)
				: proc(p) {
				query_type = QueryType::STORED;
				in_params.Append(in);
				out_params.Append(out);
				ev = cb;
			}
		};
	}
	namespace userlistenProto
	{
		/// <summary>
		/// 用户监听部分接收及发送事件
		/// </summary>
		struct EVT {
			
			inline static const char* REQ_LOGIN = "auth:login-req";//登录请求
			inline static const char* RES_LOGIN = "auth:login-res";//登录响应
			inline static const char* REQ_TOKEN_HEARTBEAT = "auth:token-heartbeat-req";//token心跳请求
			inline static const char* RES_TOKEN_HEARTBEAT = "auth:token-heartbeat-res";//token心跳响应
			inline static const char* REQ_LOGOUT = "auth:logout-req";//登出请求
			inline static const char* RES_LOGOUT = "auth:logout-res";//登出响应
			inline static const char* REQ_REGISTER = "auth:register-req";//注册请求
			inline static const char* RES_REGISTER = "auth:register-res";//注册响应
			inline static const char* REQ_FORGOT_PASSWORD = "auth:forgot-password-req";//忘记密码请求
			inline static const char* RES_FORGOT_PASSWORD = "auth:forgot-password-res";//忘记密码响应
			inline static const char* REQ_RESET_PASSWORD = "auth:reset-password-req";//重置密码请求
			inline static const char* RES_RESET_PASSWORD = "auth:reset-password-res";//重置密码响应
			inline static const char* REQ_RECONNECT = "auth:reconnect-req";//重连请求
			inline static const char* RES_RECONNECT = "auth:reconnect-res";//重连响应
			inline static const char* REQ_VERIFY_EMAIL = "auth:verify-email-req";//邮箱验证请求
			inline static const char* RES_VERIFY_EMAIL = "auth:verify-email-res";//邮箱验证响应
			inline static const char* REQ_API_EXECUTE = "auth:api-execute-req";//API执行请求
			inline static const char* RES_API_EXECUTE = "auth:api-execute-res";//API执行响应
		};
		/// <summary>
        /// 账号注册错误码
        /// </summary>
		enum class RegResult : uint32_t
		{
			OK = 0,
			USERNAME_TAKEN = 1,    // 用户名已存在
			USERNAME_INVALID = 2,    // 用户名格式非法
			PASSWORD_WEAK = 3,    // 密码强度不足
			VERIFY_CODE_BAD = 4,    // 验证码错误或已过期
			CRED_TYPE_INVALID = 5,    // 不支持的凭据类型
			PHONE_TAKEN = 6,    // 手机号已注册
			EMAIL_TAKEN = 7,    // 邮箱已注册
			REG_CLOSED = 8,    // 注册已关闭（global_config.reg_enabled = 0）
			RATE_LIMITED = 9,    // 触发限流
			OAUTH_FAILED = 10,   // 第三方平台验证失败
			INVALID_PARAM = 11,   // 参数缺失或非法
			SYSTEM_ERROR = 99,
		};

		/// <summary>
		/// 账号登录错误码
		/// </summary>
		enum class LoginResult : uint32_t
		{
			OK = 0,
			USERNAME_NOT_FOUND = 1,    // 用户名不存在
			PASSWORD_INCORRECT = 2,    // 密码错误
			ACCOUNT_BANNED = 3,    // 账号被封禁
			ACCOUNT_INACTIVE = 4,    // 账号未激活（需邮箱/手机激活）
			CRED_TYPE_INVALID = 5,    // 不支持的凭据类型
			RATE_LIMITED = 6,    // 触发限流
			OAUTH_FAILED = 7,   // 第三方平台验证失败
			INVALID_PARAM = 8,   // 参数缺失或非法
			SYSTEM_ERROR = 99,
		};
		enum class ReconnectResult : uint32_t
		{
			OK = 0,
			INVALID_PARAM = 1,          // 参数缺失或非法
			TOKEN_NOT_FOUND = 2,        // token不存在或已失效
			TOKEN_EXPIRED = 3,          // token已过期
			TOKEN_UID_MISMATCH = 4,     // token与账号不匹配
			TOKEN_SID_MISMATCH = 5,     // token与连接不匹配
			SYSTEM_ERROR = 99,
		};
		enum class TokenResult : uint32_t
		{
			OK = 0,
			INVALID_PARAM = 1,          // 参数缺失或非法
			TOKEN_NOT_FOUND = 2,        // token不存在或已失效
			TOKEN_EXPIRED = 3,          // token已过期
			TOKEN_UID_MISMATCH = 4,     // token与uid不匹配
			TOKEN_SID_MISMATCH = 5,     // token与连接sid不匹配
			SYSTEM_ERROR = 99,
		};
		const char* GetRegResultMessage(RegResult code);
		const char* GetLoginResultMessage(LoginResult code);
		const char* GetTokenResultMessage(TokenResult code);
		const char* GetReconnectResultMessage(ReconnectResult code);
		/// <summary>
        /// 用户注册请求
        /// ★ 只含「用户提供的信息」。uid / status / reg_ip / cred_hash 等一律由服务端生成，
        ///   客户端不得提交（详见字段说明）。
        /// </summary>
		struct REGISTER_REQ : public REQ_BASE
		{
			uint16_t    proto_ver = 1;   // 协议版本 —— 本序列化无 tag 无版本号，留作将来扩展
			std::string username;           // 用户名，3~20 字符；服务端做小写化存 username_lc
			std::string nickname;           // 昵称，可空；服务端校验敏感词
			uint8_t     cred_type = 1;   // 凭据类型：1密码 2手机 3邮箱 10微信 11QQ 12苹果
			std::string cred_key;           // 凭据值：密码明文 / 验证码 / 第三方 openid
			std::string cred_extra;         // 附加凭据：第三方登录的 id_token / access_token
			std::string phone;              // 手机号，可空
			std::string email;              // 邮箱，可空
			std::string verify_code;        // 验证码：手机 / 邮箱注册时必填
			std::string device_id;          // 设备唯一标识（客户端生成并持久化）
			std::string device_info;        // 设备信息：系统 / 型号 / UA，用于风控
			std::string channel;            // 注册渠道：官网 / 应用宝 / tap 等
			std::string invite_code;        // 邀请码，可空

			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% proto_ver% username% nickname% cred_type% cred_key% cred_extra
					% phone% email% verify_code% device_id% device_info% channel
					% invite_code;
			}
		};
		/// <summary>
        /// 用户注册响应
        /// </summary>
		struct REGISTER_RES : public RES_BASE
		{
			uint64_t    uid = 0;     // 成功时返回
			uint8_t     status = 0;     // 账号状态：1正常 2未激活（需邮箱/手机激活）
			std::string ticket;         // 注册即登录时下发；为空表示需单独走登录流程

			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% uid% status% ticket;
			}
		};
		/// <summary>
		/// 用户登录响应结构体
		/// ★ 序列化「字段顺序即协议、无 tag 无版本号」→ 新字段只能【末尾追加】，
		///   禁止改顺序或中间插入，否则老客户端全部解错。
		/// </summary>
		struct LOGIN_RES : public RES_BASE
		{
			std::string message;
			uint64_t    uid    = 0;      // 追加：登录成功时的账号 ID
			std::string ticket;          // 追加：登录票据（空=未签发，见 FinishLogin）
			std::string token;           // 追加：会话token
			int64_t     token_expire_at = 0; // 追加：会话token过期时间（Unix秒）
			// TODO(下一步)：追加 ZONE_INFO[] —— 登录成功后要返回区列表供客户端选区，
			//               数据来自 zone 表 + 区级 Center 上报的活性状态。
			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% message% uid% ticket% token% token_expire_at;
			}
		};
		/// <summary>
		/// 用户登录请求结构体
		/// </summary>
		struct LOGIN_REQ : public REQ_BASE
		{
			uint16_t    proto_ver = 1;
			std::string username;
			uint8_t     cred_type = 1;
			std::string cred_val;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% proto_ver% username% cred_type% cred_val;
			}
		};
		/// <summary>
		/// 会话Token心跳请求结构体
		/// </summary>
		struct TOKEN_HEARTBEAT_REQ : public REQ_BASE
		{
			uint16_t    proto_ver = 1;
			uint64_t    uid = 0;
			std::string token;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% proto_ver% uid% token;
			}
		};
		/// <summary>
		/// 会话Token心跳响应结构体
		/// </summary>
		struct TOKEN_HEARTBEAT_RES : public RES_BASE
		{
			uint64_t    uid = 0;
			std::string token;
			int64_t     token_expire_at = 0;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% uid% token% token_expire_at;
			}
		};
		/// <summary>
		/// 重连请求结构体
		/// </summary>
		struct RECONNECT_REQ : public REQ_BASE
		{
			uint16_t    proto_ver = 1;
			uint64_t    uid = 0;
			std::string token;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% proto_ver% uid% token;
			}
		};
		/// <summary>
		/// 重连响应结构体
		/// </summary>
		struct RECONNECT_RES : public RES_BASE
		{
			uint64_t    uid = 0;
			std::string token;
			int64_t     token_expire_at = 0;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% uid% token% token_expire_at;
			}
		};
	}
	namespace adminlistenProto
	{
		/// <summary>
	    /// 挑战数据
	    /// </summary>
		struct ChallengeData {
			String challenge_id;//挑战码
			int64 expire_at = 0;//过期时间戳
			bool is_expired = false;//是否已过期
			bool used = false;//是否已使用
		};
		struct ChallengesTask : QUEUE_BASE
		{
			String challenge_id;
			ChallengeData challenge_data;
			ChallengesTask() = default;
			ChallengesTask(const String& id, const ChallengeData& data, Event<Value> cb)
				: QUEUE_BASE(cb), challenge_id(id), challenge_data(data) {
			}
		};

		struct AdminUserInfo : USER_INFO
		{
			String username_lc;
			ChallengeData challenge_data;
			String token;
			int64 issued_at = 0;
			int64 expire_at = 0;
			AdminUserInfo() = default;
			AdminUserInfo(const NetLibrary::Clientinfo& ci)
				: USER_INFO(ci) {
			}
		};

		struct EVT {
			inline static const char* REQ_ADMIN_CHALLENGE = "admin:challenge-req";//管理员挑战请求
			inline static const char* RES_ADMIN_CHALLENGE = "admin:challenge-res";//管理员挑战响应
			inline static const char* REQ_ADMIN_LOGIN = "admin:login-req";//管理员登录请求
			inline static const char* RES_ADMIN_LOGIN = "admin:login-res";//管理员登录响应
			inline static const char* REQ_ADMIN_API_EXECUTE = "admin:api-execute-req";//API执行请求
			inline static const char* RES_ADMIN_API_EXECUTE = "admin:api-execute-res";//API执行响应
		};
		enum class AdminResult : uint32_t
		{
			OK = 0,
			INVALID_PARAM = 1,
			CHALLENGE_NOT_FOUND = 2,
			CHALLENGE_EXPIRED = 3,
			LOGIN_CODE_INVALID = 4,
			REPLAY_DETECTED = 5,
			USERNAME_NOT_FOUND = 6,
			SYSTEM_ERROR = 99,
		};
		/// <summary>
		/// 管理员挑战请求
		/// </summary>
		struct ADMIN_CHALLENGE_REQ : public REQ_BASE
		{
			uint16_t    proto_ver = 1;
			std::string username;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% proto_ver% username;
			}
		};
		/// <summary>
		/// 管理员挑战响应
		/// </summary>
		struct ADMIN_CHALLENGE_RES : public RES_BASE
		{
			std::string challenge_id;
			int64_t expire_at = 0;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% challenge_id% expire_at;
			}
		};
		/// <summary>
		/// 管理员登录请求结构体（登录码模式）
		/// </summary>
		struct ADMIN_LOGIN_REQ : public REQ_BASE
		{
			uint16_t    proto_ver = 1;
			std::string username;
			std::string challenge_id;
			std::string login_code;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% proto_ver% username% challenge_id% login_code;
			}
		};
		/// <summary>
		/// 管理员登录响应
		/// </summary>
		struct ADMIN_LOGIN_RES : public RES_BASE
		{
			std::string token;
			int64_t expire_at = 0;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% token% expire_at;
			}
		};
		struct ADMIN_API_EXECUTE_REQ : public REQ_BASE
		{
			uint16_t    proto_ver = 1;
			std::string api_name;//API名称
			std::string api_module;//API模块
			std::string callback;//回调事件名称
			std::string payload;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% proto_ver% api_name% api_module% callback% payload;
			}
		};
		struct ADMIN_API_EXECUTE_RES : public RES_BASE
		{
			std::string api_name;//API名称
			std::string api_module;//API模块
			std::string callback;//回调事件名称
			std::string payload;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% api_name% api_module% callback% payload;
			}
		};
	}
}
