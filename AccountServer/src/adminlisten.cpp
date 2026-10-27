#include "../AccountServer.h"

void AccountServer::adminlisten::CreateAdminUserInternal(const AccountServer::CreateAdminParams& p)
{
	String username = TrimBoth(p.username);
	String nickname = TrimBoth(p.nickname);
	String password = p.password;
	String usb_serial = TrimBoth(p.usb_serial);
	String key_file_path = TrimBoth(p.key_file_path);

	if (username.IsEmpty() || password.IsEmpty() || usb_serial.IsEmpty() || key_file_path.IsEmpty()) {
		sharedFeatures::PrintMsg("%s参数不能为空", "[CreateAdmin]");
		return;
	}

	String username_lc = ToLower(username);
	HashResult hr = PasswordHasher::Hash(password);
	if (hr.ok == false)
		return sharedFeatures::PrintMsg("%s密码哈希失败: %s", "[CreateAdmin]", hr.error);
	AccountServer::AdminUsbKeyIssuer key_issuer;
	key_issuer.SetServerSecret(this->server_secret_);
	String key_err;
	String device_secret;
	String keyfile = key_issuer.IssueKeyFile(username, hr.phc, usb_serial, 30 * 24 * 3600, &key_err, &device_secret);
	if (keyfile.IsEmpty())
		return sharedFeatures::PrintMsg("%s生成 key 文件失败: %s", "[CreateAdmin]", key_err);
	Value payload;
	payload.GetAdd("key_file_path") = key_file_path;
	payload.GetAdd("key_err") = key_err;
	payload.GetAdd("keyfile") = keyfile;
	int64 now = Upp::GetUTCSeconds(Upp::GetSysTime());
	
	Vector<Value> in = { username, username_lc, nickname, hr.phc, hr.algo, device_secret, 1, (int64)1, now };
	Vector<String> out = { "@ret_code" };
	AssetLib::BinaryData payload_bin(AsJSON(payload).ToStd());
	assetEventBus.Emit("mysql_stored", "HandleCreateAdmin", "sp_admin_creat", WithDeepCopy<Vector<Value>>(in), WithDeepCopy<Vector<String>>(out), payload_bin);
}
void AccountServer::adminlisten::HandleCreateAdmin(const AssetLib::EventArgs& args)
{
	if (args.size() < 2)
		return;
	const std::string* arg1 = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
	const AssetLib::BinaryData* arg2 = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 1);
	Value sqlData = ParseJSON(arg1->c_str());
	Value payload = ParseJSON(*arg2);
	if (sqlData["@ret_code"] == 1) 
		return sharedFeatures::PrintMsg("%s账号已存在", "[CreateAdmin]");
	if (sqlData["@ret_code"] == 0)
		return sharedFeatures::PrintMsg("%s账号创建失败", "[CreateAdmin]");
	if (sqlData["@ret_code"] == 11)
	{
		if (!SaveFile(~payload["key_file_path"], payload["keyfile"]))
			return sharedFeatures::PrintMsg("%s写入 key 文件失败", "[CreateAdmin]");
	}
}


void AccountServer::adminlisten::StartCreateAdminWizardAsync()
{
	CreateAdminParams p;
	sharedFeatures::PrintMsg("%s 请输入用户名: ", "[CreateAdmin]");
	std::string in;
	if (!std::getline(std::cin, in)) return;
	p.username = in.c_str();

	sharedFeatures::PrintMsg("%s 请输入昵称(可留空): ", "[CreateAdmin]");
	in.clear();
	if (!std::getline(std::cin, in)) return;
	p.nickname = in.c_str();
	if (p.nickname.IsEmpty())
		p.nickname = p.username;

	String selected_root;
#if defined(_WIN32) || defined(PLATFORM_POSIX)
	Vector<RemovableDriveInfo> drives = AdminUsbKeyIssuer::EnumerateRemovableDrives();
	if (drives.IsEmpty()) {
		sharedFeatures::PrintMsg("%s 未检测到可移动磁盘，请插入U盘后重试", "[CreateAdmin]");
		return;
	}
	sharedFeatures::PrintMsg("%s 检测到可移动磁盘:", "[CreateAdmin]");
	for (int i = 0; i < drives.GetCount(); ++i) {
		sharedFeatures::PrintMsg("  [%d] %s", i, drives[i].root);
		if (!drives[i].device_serial.IsEmpty())
			sharedFeatures::PrintMsg("  Serial=%s", drives[i].device_serial);
		else
			sharedFeatures::PrintMsg("  Serial=<读取失败>");
		sharedFeatures::PrintMsg("\n");
	}
	sharedFeatures::PrintMsg("%s 请选择U盘编号: ", "[CreateAdmin]");
	in.clear();
	if (!std::getline(std::cin, in)) return;
	int idx = atoi(in.c_str());
	if (idx < 0 || idx >= drives.GetCount()) {
		sharedFeatures::PrintMsg("%s 编号无效", "[CreateAdmin]");
		return;
	}
	if (drives[idx].device_serial.IsEmpty()) {
		sharedFeatures::PrintMsg("%s 该U盘设备序列号读取失败，请更换U盘或重试", "[CreateAdmin]");
		return;
	}
	p.usb_serial = drives[idx].device_serial;
	selected_root = drives[idx].root;
	sharedFeatures::PrintMsg("%s 已自动读取U盘设备序列号: %s", "[CreateAdmin]", p.usb_serial);
#else
	sharedFeatures::PrintMsg("%s 当前平台未实现自动读取U盘设备序列号，请手动输入: ", "[CreateAdmin]");
	in.clear();
	if (!std::getline(std::cin, in)) return;
	p.usb_serial = in.c_str();
#endif

	String suggested;
	if (selected_root.IsEmpty())
		suggested = "E:/admin.key";
	else if (selected_root.EndsWith("/") || selected_root.EndsWith("\\"))
		suggested = selected_root + "admin.key";
	else
		suggested = selected_root + "/admin.key";
	sharedFeatures::PrintMsg("%s 请输入key文件完整路径(回车默认 %s): ", "[CreateAdmin]", suggested);
	in.clear();
	if (!std::getline(std::cin, in)) return;
	p.key_file_path = TrimBoth(String(in.c_str()));
	if (p.key_file_path.IsEmpty())
		p.key_file_path = suggested;

	sharedFeatures::PrintMsg("%s 请输入密码: ", "[CreateAdmin]");
	std::string p1;
	if (!std::getline(std::cin, p1) || p1.empty()) {
		sharedFeatures::PrintMsg("%s 密码不能为空", "[CreateAdmin]");
		return;
	}
	sharedFeatures::PrintMsg("%s 请再次输入密码: ", "[CreateAdmin]");
	std::string p2;
	if (!std::getline(std::cin, p2) || p2.empty() || p1 != p2) {
		sharedFeatures::PrintMsg("%s 两次输入密码不一致", "[CreateAdmin]");
		return;
	}
	p.password = p1.c_str();

	sharedFeatures::PrintMsg("%s 参数已接收，正在后台创建，请稍候...", "[CreateAdmin]");
	String out;
	CreateAdminUserInternal(p);
}
AccountServer::adminlisten::adminlisten()
{
	script_service_ = std::make_unique<ApiLibrary::IScriptService>();
	assetEventBus.On("HandleCreateAdmin", THISFN(HandleCreateAdmin));
	assetEventBus.On("HandleChallengeResponse", THISFN(HandleChallengeResponse));
	assetEventBus.On("HandleLoginResponse", THISFN(HandleLoginResponse));
}
AccountServer::adminlisten::~adminlisten()
{
	StopAdminListen();
}

void AccountServer::adminlisten::ReceiveMessages(const AssetLib::EventArgs& args)
{
	if (args.size() != 1)
		return;
	const AssetLib::BinaryData* data = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 0);
	if (!data)
		return;
	queue_.CreatQueue(THISFN(ProcessQueue), true, *data);
}


void AccountServer::adminlisten::ProcessQueue(QUEUE_BASE* data)
{
	NetLibrary::SockData sd;
	AssetLib::SerializerFactory::Deserialize(sd, data->payload);
	if (sd.isBinary)
	{
		REQ_BASE base;
		AssetLib::SerializerFactory::Deserialize(base, sd.data);
		if (base.ev == adminlistenProto::EVT::REQ_ADMIN_CHALLENGE)
		{
			adminlistenProto::ADMIN_CHALLENGE_REQ req;
			AssetLib::SerializerFactory::Deserialize(req, sd.data);
			HandleChallengeRequest(req, sd.sid, sd.ackId);
		}
		else if (base.ev == adminlistenProto::EVT::REQ_ADMIN_LOGIN)
		{
			adminlistenProto::ADMIN_LOGIN_REQ loginReq;
			AssetLib::SerializerFactory::Deserialize(loginReq, sd.data);
			HandleLoginRequest(loginReq, sd.sid, sd.ackId);
		}
		else if (base.ev == adminlistenProto::EVT::REQ_ADMIN_API_EXECUTE)
		{
			adminlistenProto::ADMIN_API_EXECUTE_REQ apiReq;
			AssetLib::SerializerFactory::Deserialize(apiReq, sd.data);
			HandleApiRequest(apiReq, sd.sid, sd.ackId);
		}
	}
	else
	{
		Value jsonData = ParseJSON(sd.data);
		sharedFeatures::PrintMsg("%s 接收到非二进制数据，忽略", "[AdminListen]");
	}
}

void AccountServer::adminlisten::StartAdminListen(const NetLibrary::njson& conf)
{
	using namespace adminlistenProto;
	if (net_interface_)
		return; // 已经启动，无需重复启动
	if (sharedFeatures::IsValidSvrconf(conf) == false)
		throw Upp::Exc("Invalid admin listen configuration.");
	config_ = conf;
	if (config_.contains("challenge_ttl_sec") && config_["challenge_ttl_sec"].is_number_integer())
		challenge_ttl_sec_ = config_["challenge_ttl_sec"].get<int64>();
	if (challenge_ttl_sec_ <= 0)
		challenge_ttl_sec_ = 60;
	if (config_.contains("admin_token_ttl_sec") && config_["admin_token_ttl_sec"].is_number_integer())
		token_ttl_sec_ = config_["admin_token_ttl_sec"].get<int64>();
	if (token_ttl_sec_ <= 0)
		token_ttl_sec_ = 1800;
	if (config_.contains("login_code_window_sec") && config_["login_code_window_sec"].is_number_integer())
		login_code_service_.SetTimeWindowSec(config_["login_code_window_sec"].get<int64>());
	if (config_.contains("admin_key_sign_secret") && config_["admin_key_sign_secret"].is_string())
		server_secret_ = config_["admin_key_sign_secret"].get<std::string>().c_str();
	net_interface_ = NetLibrary::CreateNetLibrary(true);
	try {
		if (!script_service_)
			script_service_ = std::make_unique<ApiLibrary::IScriptService>();

		ApiLibrary::ScriptRuntimeConfig script_cfg;
		if (!config_.contains("admin_api_path") || !config_["admin_api_path"].is_string())
			throw Upp::Exc("Missing required config: admin_api_path");
		script_cfg.script_root = config_["admin_api_path"].get<std::string>().c_str();
		if (IsNull(script_cfg.script_root) || script_cfg.script_root.empty())
			throw Upp::Exc("Invalid config: admin_api_path is empty");
		if (config_.contains("admin_api_entry") && config_["admin_api_entry"].is_string())
			script_cfg.entry_file = config_["admin_api_entry"].get<std::string>().c_str();
		if (config_.contains("admin_api_workers") && config_["admin_api_workers"].is_number_integer())
			script_cfg.worker_count = config_["admin_api_workers"].get<int>();
		if (config_.contains("admin_api_timeout_ms") && config_["admin_api_timeout_ms"].is_number_integer())
			script_cfg.default_timeout_ms = config_["admin_api_timeout_ms"].get<int>();
		if (config_.contains("admin_api_op_limit") && config_["admin_api_op_limit"].is_number_integer())
			script_cfg.default_op_limit = config_["admin_api_op_limit"].get<int>();

		script_cfg.on_api_execute = [this](const AssetLib::EventArgs& args) {
			this->HandleApiExecute(args);
		};

		std::string script_err;
		if (!script_service_->Start(script_cfg, &script_err))
			throw Upp::Exc(Format("ScriptService start failed: %s", script_err.c_str()));

		timeAsync->CreateTask({ this, &adminlisten::CleanupAdminTokens }, 100, MakeShared<TIMETASK_BASE>());
		timeAsync->CreateTask({ this, &adminlisten::CleanupChallenges }, 100, MakeShared<TIMETASK_BASE>());
		this->StartNetServer(config_);
	}
	catch (const Upp::Exc& ex) {
		LOG("Error starting admin listen server: " << ex);
		throw;
	}
	catch (const std::exception& ex) {
		LOG("Standard exception starting admin listen server: " << ex.what());
		throw;
	}
}

void AccountServer::adminlisten::StopAdminListen()
{
	this->StopNetServer();
	if (script_service_)
		script_service_->Stop();
}

void AccountServer::adminlisten::CleanupChallenges(SharedPtr<TIMETASK_BASE> task)
{
	using namespace adminlistenProto;
	const int64 now = AdminLoginCodeService::NowSec();
	Vector<Uuid> expired;
	client_map_.ForEachAs<AdminUserInfo>([&](const Uuid& tk, const AdminUserInfo& s) {
		if (s.challenge_data.expire_at >= now)
			expired.Add(tk);
	});
	for (const auto& tk : expired)
		client_map_.UpdateAs<AdminUserInfo>(tk, [&](AdminUserInfo& s) {
		s.challenge_data.is_expired = true;
	});
}

void AccountServer::adminlisten::CleanupAdminTokens(SharedPtr<TIMETASK_BASE> task)
{
	using namespace adminlistenProto;
	const int64 now = AdminLoginCodeService::NowSec();
	Vector<Uuid> expired;
	client_map_.ForEachAs<AdminUserInfo>([&](const Uuid& tk, const AdminUserInfo& s) {
		if (s.expire_at <= now)
			expired.Add(tk);
	});
	for (const auto& tk : expired)
		client_map_.Erase(tk);
}

void AccountServer::adminlisten::HandleClientConnect(const AssetLib::BinaryData& data)
{
	using namespace adminlistenProto;
	NetLibrary::Clientinfo info;
	AssetLib::SerializerFactory::Deserialize(info, data);
	client_map_.InsertDerived(ScanUuid(info.sid.c_str()), MakeShared<AdminUserInfo>(info));
	sharedFeatures::PrintMsg("Client connected: sid=%s, ip=%s, isSocketIO=%s", info.sid.c_str(), info.ip.c_str(), info.isSocketIO ? "true" : "false");
}

void AccountServer::adminlisten::HandleChallengeRequest(const adminlistenProto::ADMIN_CHALLENGE_REQ& data, String sid, int ackId)
{
	using namespace adminlistenProto;
	ADMIN_CHALLENGE_RES res;
	if (data.proto_ver != 1)
		return HandleResponse(sid,ackId,EVT::RES_ADMIN_CHALLENGE, false, (uint32_t)AdminResult::INVALID_PARAM, "协议版本不支持", res);
	String username_lc = Upp::ToLower(Upp::TrimBoth(String(data.username.c_str())));
	if (username_lc.IsEmpty())
		return HandleResponse(sid, ackId, EVT::RES_ADMIN_CHALLENGE, false, (uint32_t)AdminResult::INVALID_PARAM, "用户名不能为空", res);
	ValueMap payload = { {"sid", sid}, {"ackId", ackId} };
	AssetLib::BinaryData payload_bin(AsJSON(payload).ToStd());
	assetEventBus.Emit("mysql_query_ret", "HandleChallengeResponse", "SELECT * FROM admin_user WHERE username_lc = ? LIMIT 1", WithDeepCopy<Vector<Value>>(Vector<Value>{ username_lc }), payload_bin);
}

void AccountServer::adminlisten::HandleChallengeResponse(const AssetLib::EventArgs& args)
{
	using namespace adminlistenProto;
	ADMIN_CHALLENGE_RES res;
	if (args.size() < 2)
		return;
	const std::string* arg1 = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
	const AssetLib::BinaryData* arg2 = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 1);
	if (!arg1 || !arg2)
		return;
	Value sqlData = ParseJSON(arg1->c_str());
	Value payload = ParseJSON(*arg2);
	Value sid = payload["sid"];
	Value ackId = payload["ackId"];
	if(sqlData.IsError())	
		return HandleResponse(sid, ackId, EVT::RES_ADMIN_CHALLENGE, false, (uint32_t)AdminResult::SYSTEM_ERROR, "查询管理员失败", res);
	if (sqlData.IsNull())
		return HandleResponse(sid, ackId, EVT::RES_ADMIN_CHALLENGE, false, (uint32_t)AdminResult::USERNAME_NOT_FOUND, "管理员不存在", res);
	String cid = sharedFeatures::GenerateRandomString(16);
	if (cid.IsEmpty())
		return HandleResponse(sid, ackId, EVT::RES_ADMIN_CHALLENGE, false, (uint32_t)AdminResult::SYSTEM_ERROR, "生成挑战码失败", res);
	int64 expire = AdminLoginCodeService::NowSec() + challenge_ttl_sec_;
	client_map_.UpdateAs<AdminUserInfo>(ScanUuid(~sid), [&](AdminUserInfo& info) {
		info.challenge_data.challenge_id = cid;
		info.challenge_data.expire_at = expire;
		info.challenge_data.used = false;
		});
	res.challenge_id = cid;
	res.expire_at = expire;
	HandleResponse(sid, ackId, EVT::RES_ADMIN_CHALLENGE, true, (uint32_t)AdminResult::OK, "ok", res);
}

void AccountServer::adminlisten::HandleLoginRequest(const adminlistenProto::ADMIN_LOGIN_REQ& data, String sid, int ackId	)
{
	using namespace adminlistenProto;
	ADMIN_LOGIN_RES res;
	if (data.proto_ver != 1)
		return HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, false, (uint32_t)AdminResult::INVALID_PARAM, "协议版本不支持", res);
	String username_lc = Upp::ToLower(Upp::TrimBoth(String(data.username.c_str())));
	String challenge_id = Upp::TrimBoth(String(data.challenge_id.c_str()));
	String login_code = Upp::TrimBoth(String(data.login_code.c_str()));
	if (username_lc.IsEmpty() || challenge_id.IsEmpty() || login_code.IsEmpty())
		return HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, false, (uint32_t)AdminResult::INVALID_PARAM, "参数不完整", res);

	{
		SharedPtr<AdminUserInfo> info = nullptr;
		if (!client_map_.TryGetCopyAs<SharedPtr<AdminUserInfo>>(ScanUuid(sid), info))
			return HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, false, (uint32_t)AdminResult::CHALLENGE_NOT_FOUND, "用户不存在", res);
		if (info->challenge_data.challenge_id != challenge_id)
			return HandleResponse(sid, ackId,EVT::RES_ADMIN_LOGIN, false, (uint32_t)AdminResult::CHALLENGE_NOT_FOUND, "挑战码不存在", res);
		if (info->challenge_data.is_expired) {
			client_map_.Erase(ScanUuid(sid));
			return HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, false, (uint32_t)AdminResult::CHALLENGE_EXPIRED, "挑战码已过期", res);
		}
		if (info->challenge_data.used)
			return HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, false, (uint32_t)AdminResult::REPLAY_DETECTED, "挑战码已使用", res);
		if (info->username_lc != username_lc)
			return HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, false, (uint32_t)AdminResult::INVALID_PARAM, "用户名与挑战码不匹配", res);
	}
	ValueMap payload = { { "sid", sid },{ "ackId", ackId },{ "username_lc",username_lc },{ "challenge_id", challenge_id },{ "login_code", login_code } };
	AssetLib::BinaryData payload_bin(AsJSON(payload).ToStd());
	assetEventBus.Emit("mysql_query_ret", "HandleLoginResponse", "SELECT * FROM admin_user WHERE username_lc = ? LIMIT 1", WithDeepCopy<Vector<Value>>(Vector<Value>{ username_lc }), payload_bin);
}
void AccountServer::adminlisten::HandleLoginResponse(const AssetLib::EventArgs& args)
{
	using namespace adminlistenProto;
	ADMIN_LOGIN_REQ req;
	ADMIN_LOGIN_RES res;
	if (args.size() < 2)
		return;
	const std::string* arg1 = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
	const AssetLib::BinaryData* arg2 = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 1);
	if (!arg1 || !arg2)
		return;
	Value sqlData = ParseJSON(arg1->c_str());
	Value payload = ParseJSON(*arg2);
	String sid = payload["sid"].ToStd();
	int ackId = payload["ackId"];
	if (sqlData.IsError())
		return HandleResponse(sid, ackId, adminlistenProto::EVT::RES_ADMIN_LOGIN, false, (uint32_t)adminlistenProto::AdminResult::SYSTEM_ERROR, "查询管理员失败", adminlistenProto::ADMIN_LOGIN_RES());
	if (sqlData.IsNull())
		return HandleResponse(sid, ackId, adminlistenProto::EVT::RES_ADMIN_LOGIN, false, (uint32_t)adminlistenProto::AdminResult::USERNAME_NOT_FOUND, "管理员不存在", adminlistenProto::ADMIN_LOGIN_RES());
	if (sqlData["device_secret"].IsNull() || sqlData["device_secret"].ToStd().empty())
		return HandleResponse(sid, ackId, adminlistenProto::EVT::RES_ADMIN_LOGIN, false, (uint32_t)adminlistenProto::AdminResult::USERNAME_NOT_FOUND, "管理员未绑定设备密钥", adminlistenProto::ADMIN_LOGIN_RES());
	AdminLoginCodePayload logincode;
	String err;
	if (!login_code_service_.VerifyLoginCode(payload["login_code"], sqlData["device_secret"], payload["username_lc"], payload["challenge_id"], &logincode, &err))
		return HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, false,
			(uint32_t)(err.Find("replay") >= 0 ? AdminResult::REPLAY_DETECTED : AdminResult::LOGIN_CODE_INVALID),
			(~err && *~err) ? ~err : "登录码校验失败", res);
	const int64 now = AdminLoginCodeService::NowSec();
	const int64 expire_at = now + token_ttl_sec_;
	String token = sharedFeatures::GenerateAdminToken();
	client_map_.UpdateAs<AdminUserInfo>(ScanUuid(sid), [&](AdminUserInfo& info) {
		info.username_lc = payload["username_lc"];
		info.token = token;
		info.issued_at = now;
		info.expire_at = expire_at;
	});
	res.token = token.ToStd();
	res.expire_at = expire_at;
	HandleResponse(sid, ackId, EVT::RES_ADMIN_LOGIN, true, (uint32_t)AdminResult::OK, "ok", res);
}
void AccountServer::adminlisten::HandleApiRequest(const adminlistenProto::ADMIN_API_EXECUTE_REQ& data, String sid, int ackid)
{
	if (!script_service_)
		return HandleResponse(sid, ackid, adminlistenProto::EVT::RES_ADMIN_API_EXECUTE, false, (uint32_t)adminlistenProto::AdminResult::SYSTEM_ERROR, "API接口未初始化", adminlistenProto::ADMIN_API_EXECUTE_RES());
	ValueMap payload = { 
		{ "info",
		    ValueMap{
			    { "sid", sid },
			    { "ackId", ackid }
	        } 
		},
		{ "data",ParseJSON(data.payload.c_str())}
	};
	ApiLibrary::ScriptInvokeRequest req;
	req.sid = sid;
	req.event_name = data.ev.c_str();
	req.payload = AsJSON(payload).ToStd();
	req.timeout_ms = 200;
	req.op_limit = 200000;
	if (config_.contains("admin_api_timeout_ms") && config_["admin_api_timeout_ms"].is_number_integer())
		req.timeout_ms = config_["admin_api_timeout_ms"].get<int>();
	if (config_.contains("admin_api_op_limit") && config_["admin_api_op_limit"].is_number_integer())
		req.op_limit = config_["admin_api_op_limit"].get<int>();
	req.trace_id = sharedFeatures::GenerateRandomString(12);

	script_service_->PostInvoke(req);
}
void AccountServer::adminlisten::HandleApiExecute(const AssetLib::EventArgs& args)
{
	if (args.size() != 1)
		return;
	const std::string* data = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
	if (!data)
		return;
	Value jsonData = ParseJSON(data->c_str());
	if (jsonData["module"] == "mysql")
	{
		if(jsonData["type"] == "queryret" || jsonData["type"] == "querynull")
		{
			Vector<Value> params;
			for (auto v : jsonData["params"])
				params.Add(v);
			AssetLib::BinaryData payload_bin(*data);
			assetEventBus.Emit("mysql_query_ret", "HandleApiResponse", jsonData["sql"].ToStd(), WithDeepCopy<Vector<Value>>(params), payload_bin);
		}
		if (jsonData["type"] == "stored")
		{
			Vector<Value> in_params;
			Vector<String> out_params;
			for (auto v : jsonData["in_params"])
				in_params.Add(v);
			for (auto v : jsonData["out_params"])
				out_params.Add(v.ToStd());
			AssetLib::BinaryData payload_bin(*data);
			assetEventBus.Emit("mysql_stored", "HandleApiResponse", jsonData["proc"].ToStd(), WithDeepCopy<Vector<Value>>(in_params), WithDeepCopy<Vector<String>>(out_params), payload_bin);
		}
	}
	if (jsonData["module"] == "send")
	{
		adminlistenProto::ADMIN_API_EXECUTE_RES res;
		res.callback = jsonData["callback"].ToStd();
		res.payload = AsJSON(jsonData["data"]);
		HandleResponse(jsonData["info"]["sid"], jsonData["info"]["ackId"], adminlistenProto::EVT::RES_ADMIN_API_EXECUTE, true, (uint32_t)adminlistenProto::AdminResult::OK, "ok", res);
	}
}

void AccountServer::adminlisten::HandleApiResponse(const AssetLib::EventArgs& args)
{
	if (args.size() < 2)
		return;
	const std::string* arg1 = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
	const AssetLib::BinaryData* arg2 = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 1);
	Value sqlData = ParseJSON(arg1->c_str());
	Value payload = ParseJSON(*arg2);
	if(payload["api_callback"])
	{
		if (!script_service_)
			return;
		std::string sid = payload["info"]["sid"].ToStd();
		script_service_->PostCallback(sid, payload["api_callback"].ToStd(), arg1->c_str());
	}
	else
	{
		adminlistenProto::ADMIN_API_EXECUTE_RES res;
		res.api_name = payload["api_name"].ToStd();
		res.api_module = payload["api_module"].ToStd();
		res.callback = payload["callback"].ToStd();
		res.payload = arg1->c_str();
		HandleResponse(payload["info"]["sid"], payload["info"]["ackId"], adminlistenProto::EVT::RES_ADMIN_API_EXECUTE, true, (uint32_t)adminlistenProto::AdminResult::OK, "ok", res);
	}
}