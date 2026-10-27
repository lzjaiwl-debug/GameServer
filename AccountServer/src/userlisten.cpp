#include "../AccountServer.h"

namespace {
using namespace AccountServer;
using namespace AccountServer::userlistenProto;

bool MapAuthCode(int res_code, bool has_row, uint32_t& out_code, const char*& out_msg)
{
	if (!has_row)
	{
		out_code = (uint32_t)LoginResult::USERNAME_NOT_FOUND;
		out_msg = "用户名不存在或密码错误";
		return false;
	}

	switch (res_code)
	{
	case 0:
		return true;
	case 2:
		out_code = (uint32_t)LoginResult::USERNAME_NOT_FOUND;
		out_msg = "用户名不存在或密码错误";
		return false;
	case 3:
		out_code = (uint32_t)LoginResult::ACCOUNT_INACTIVE;
		out_msg = "账号状态异常";
		return false;
	case 4:
		out_code = (uint32_t)LoginResult::RATE_LIMITED;
		out_msg = "登录失败次数过多，请稍后再试";
		return false;
	case 5:
		out_code = (uint32_t)LoginResult::ACCOUNT_BANNED;
		out_msg = "账号已被封禁";
		return false;
	case 6:
		out_code = (uint32_t)LoginResult::SYSTEM_ERROR;
		out_msg = "凭证状态异常，请联系客服";
		return false;
	default:
		out_code = (uint32_t)LoginResult::SYSTEM_ERROR;
		out_msg = "服务繁忙，请稍后再试";
		return false;
	}
}
}

AccountServer::userlisten::~userlisten()
{
	StopUserListen();
}

void AccountServer::userlisten::StartUserListen(const NetLibrary::njson& conf)
{
	if (net_interface_)
		return;
	if (!sharedFeatures::IsValidSvrconf(conf))
		throw Upp::Exc("Invalid user listen configuration.");

	config_ = conf;
	if (conf.contains("reg_enabled") && conf["reg_enabled"].is_boolean())
		reg_enabled = conf["reg_enabled"].get<bool>();
	else
		reg_enabled = g_cfg.register_enabled;

	if (conf.contains("token_ttl_sec") && conf["token_ttl_sec"].is_number_integer())
		token_ttl_sec_ = conf["token_ttl_sec"].get<int64>();
	else if (g_cfg.session_expire_sec > 0)
		token_ttl_sec_ = g_cfg.session_expire_sec;
	if (token_ttl_sec_ <= 0)
		token_ttl_sec_ = 300;

	int64 sweep_sec = 1;
	if (conf.contains("token_sweep_sec") && conf["token_sweep_sec"].is_number_integer())
		sweep_sec = conf["token_sweep_sec"].get<int64>();
	if (sweep_sec <= 0)
		sweep_sec = 1;
	token_sweep_cycle_ = (int)max<int64>(1, sweep_sec * 100);

	if (conf.contains("ticket_key_login") && conf["ticket_key_login"].is_string())
		ticket_issuer_.SetKey(TicketType::LOGIN, String(conf["ticket_key_login"].get<std::string>().c_str()));
	if (conf.contains("ticket_key_zone") && conf["ticket_key_zone"].is_string())
		ticket_issuer_.SetKey(TicketType::ZONE, String(conf["ticket_key_zone"].get<std::string>().c_str()));
	if (conf.contains("ticket_key_enter") && conf["ticket_key_enter"].is_string())
		ticket_issuer_.SetKey(TicketType::ENTER, String(conf["ticket_key_enter"].get<std::string>().c_str()));

	CredentialWorker::Instance().Start();
	timeAsync->CreateTask({ this, &userlisten::SweepTokenSessions }, token_sweep_cycle_, MakeShared<TIMETASK_BASE>());
	
	try {
		this->StartNetServer(config_);
	}
	catch (const Upp::Exc& ex) {
		LOG("Error starting user listen server: " << ex);
		throw;
	}
	catch (const std::exception& ex) {
		LOG("Standard exception starting user listen server: " << ex.what());
		throw;
	}
}

void AccountServer::userlisten::StopUserListen()
{
	StopNetServer();
	CredentialWorker::Instance().Stop();
}

void AccountServer::userlisten::ReceiveMessages(const AssetLib::EventArgs& args)
{
	if (args.size() != 1)
		return;
	const AssetLib::BinaryData* data = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 0);
	if (!data)
		return;
	queue_.CreatQueue(THISFN(ProcessQueue), true, *data);
}

void AccountServer::userlisten::ProcessQueue(QUEUE_BASE* data)
{
	if (!data->payload)
		return;
	NetLibrary::SockData sd;
	AssetLib::SerializerFactory::Deserialize(sd, data->payload);
	if (!sd.isBinary)
		return;

	REQ_BASE base;
	AssetLib::SerializerFactory::Deserialize(base, sd.data);
	String sid = sd.sid.c_str();
	if (sid.IsEmpty())
		return;

	if (base.ev == userlistenProto::EVT::REQ_LOGIN)
	{
		userlistenProto::LOGIN_REQ loginReq;
		AssetLib::SerializerFactory::Deserialize(loginReq, sd.data);
		HandleLoginRequest(loginReq, sid, sd.ackId);
	}
	else if (base.ev == userlistenProto::EVT::REQ_REGISTER)
	{
		userlistenProto::REGISTER_REQ registerReq;
		AssetLib::SerializerFactory::Deserialize(registerReq, sd.data);
		HandleRegisterRequest(registerReq, sid, sd.ackId);
	}
	else if (base.ev == userlistenProto::EVT::REQ_TOKEN_HEARTBEAT)
	{
		userlistenProto::TOKEN_HEARTBEAT_REQ req;
		AssetLib::SerializerFactory::Deserialize(req, sd.data);
		HandleTokenHeartbeatRequest(req, sid, sd.ackId);
	}
	else if(base.ev == userlistenProto::EVT::RES_RECONNECT)
	{
		userlistenProto::RECONNECT_REQ res;
		AssetLib::SerializerFactory::Deserialize(res, sd.data);
		HandleReconnectRequest(res, sid, sd.ackId);
	}
}
void AccountServer::userlisten::HandleClientConnect(const AssetLib::BinaryData& data)
{
	NetLibrary::Clientinfo info;
	AssetLib::SerializerFactory::Deserialize(info, data);
	client_map_.InsertDerived(ScanUuid(info.sid.c_str()), MakeShared<PlayerInfo>(info));
	sharedFeatures::PrintMsg("Client connected: sid=%s, ip=%s, isSocketIO=%s",
		info.sid.c_str(),
		info.ip.c_str(),
		info.isSocketIO ? "true" : "false");
}
void AccountServer::userlisten::HandleClientDisconnect(const AssetLib::BinaryData& data)
{
	Uuid sid = ScanUuid(data);

	SharedPtr<PlayerInfo> userinfo = nullptr;
	client_map_.TryGetCopyAs(sid, userinfo);
	if (!userinfo)
	{
		client_map_.Erase(sid);
		sharedFeatures::PrintMsg("Client disconnected: sid=%s, ip=%s, isSocketIO=%s",
			~sid.ToStringWithDashes(),
			"unknown",
			"unknown");
		return;
	}
	int64 now = Upp::GetUTCSeconds(Upp::GetSysTime());
	if(userinfo->token_session.token.IsEmpty() || userinfo->token_session.expire_at <= now)
	{
		client_map_.Erase(sid);
		sharedFeatures::PrintMsg("Client disconnected: sid=%s, ip=%s, isSocketIO=%s",
			~sid.ToStringWithDashes(),
			userinfo ? userinfo->clientinfo.ip.c_str() : "unknown",
			userinfo ? (userinfo->clientinfo.isSocketIO ? "true" : "false") : "unknown");
	}
	else
		sharedFeatures::PrintMsg("Client disconnected: sid=%s, ip=%s, isSocketIO=%s, token not expired",
			~sid.ToStringWithDashes(),
			userinfo ? userinfo->clientinfo.ip.c_str() : "unknown",
			userinfo ? (userinfo->clientinfo.isSocketIO ? "true" : "false") : "unknown");
}

void AccountServer::userlisten::HandleRegisterRequest(const userlistenProto::REGISTER_REQ& args, const String& sid, int ackid)
{
	userlistenProto::REGISTER_RES res;
	auto userinfo = MakeShared<USER_INFO>();
	client_map_.TryGetCopy(ScanUuid(sid), userinfo);
	if (BasicVerification(userlistenProto::EVT::RES_REGISTER, args.proto_ver, args.username, args.cred_key, args.cred_type, res) == false)
		return HandleResponse(userinfo->clientinfo.sid, ackid, res);

	if (!reg_enabled)
		return HandleResponse(sid, ackid, userlistenProto::EVT::RES_REGISTER, false, (uint32_t)userlistenProto::RegResult::REG_CLOSED,"注册已关闭", res);

	String username_lc = Upp::ToLower(args.username.c_str());
	CredentialWorker::Instance().AsyncHash(String(args.cred_key.c_str()), [this, args, sid, username_lc, ackid](HashResult hr) {
		if (hr.ok == false)
			return HandleResponse(sid, ackid, userlistenProto::EVT::RES_REGISTER, false, (uint32_t)userlistenProto::RegResult::SYSTEM_ERROR, hr.error, userlistenProto::REGISTER_RES());
		ValueMap payload = { { "sid", sid },{"ackid", ackid}};
		AssetLib::BinaryData payload_bin = AsJSON(payload).ToStd();
		assetEventBus.Emit("mysql_stored", "HandleRegisterResponse", "sp_account_register", WithDeepCopy< Vector<Value>>(Vector<Value>{ args.username, username_lc, args.nickname,
			(int)args.cred_type, hr.phc, hr.algo,
			args.cred_key,
			args.phone, args.email, args.device_id, args.device_info, args.channel,
			Upp::GetUTCSeconds(Upp::GetSysTime()) }), WithDeepCopy<Vector<String>>(Vector<String>{"@out_uid", "@out_result"}), payload_bin);
	});
}

void AccountServer::userlisten::HandleRegisterResponse(const AssetLib::EventArgs& args)
{
	if (args.size() != 2)
		return;
	const std::string* arg1 = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
	const AssetLib::BinaryData* arg2 = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 1);
	Value sqlData = ParseJSON(arg1->c_str());
	Value payload = ParseJSON(*arg2);
	if (sqlData.IsError())
		return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_REGISTER, false, (uint32_t)userlistenProto::RegResult::SYSTEM_ERROR, "注册用户出现错误，用户名可能已被注册", userlistenProto::REGISTER_RES());
	int result = sqlData["@out_result"].IsNull() ? 99 : (int)sqlData["@out_result"];
	if (result == 1)
		return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_REGISTER, false, (uint32_t)userlistenProto::RegResult::USERNAME_TAKEN, "用户名重复", userlistenProto::REGISTER_RES());
	if (result == 2)
		return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_REGISTER, false, (uint32_t)userlistenProto::RegResult::PHONE_TAKEN, "手机号重复", userlistenProto::REGISTER_RES());
	if (result == 3)
		return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_REGISTER, false, (uint32_t)userlistenProto::RegResult::EMAIL_TAKEN, "邮箱重复", userlistenProto::REGISTER_RES());
	if (result != 0)
		return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_REGISTER, false, (uint32_t)userlistenProto::RegResult::SYSTEM_ERROR, "注册失败，请稍后重试", userlistenProto::REGISTER_RES());
	HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_REGISTER, true, (uint32_t)userlistenProto::RegResult::OK, "注册成功", userlistenProto::REGISTER_RES());
}

void AccountServer::userlisten::HandleLoginRequest(const userlistenProto::LOGIN_REQ& args, const String& sid, int ackid)
{
	using namespace userlistenProto;
	LOGIN_RES res;

	if (BasicVerification(EVT::RES_LOGIN, args.proto_ver, args.username, args.cred_val, args.cred_type, res) == false)
		return HandleResponse(sid, ackid, res);

	if (!g_cfg.login_enabled)
		return HandleResponse(sid, ackid, EVT::RES_LOGIN, false, (uint32_t)LoginResult::SYSTEM_ERROR, "登录维护中，请稍后再试", res);

	String username_lc = Upp::ToLower(args.username.c_str());
	int64 now = Upp::GetUTCSeconds(Upp::GetSysTime());
	ValueMap payload = { { "sid", sid },{ "ackid", ackid },{ "cred_val", args.cred_val },{"cred_type", (int)args.cred_type}};
	AssetLib::BinaryData payload_bin(AsJSON(payload).ToStd());
	assetEventBus.Emit("mysql_query_ret", "HandleLoginResponse", "CALL sp_account_login_auth(?, ?, ?)", WithDeepCopy<Vector<Value>>(Vector<Value>{ username_lc, (int)args.cred_type, now }), payload_bin);
}

void AccountServer::userlisten::HandleLoginResponse(const AssetLib::EventArgs& args)
{
	if (args.size() < 2)
		return;
	const std::string* arg1 = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
	const AssetLib::BinaryData* arg2 = AssetLib::EventBus<>::ArgAs<const AssetLib::BinaryData>(args, 1);
	if (!arg1 || !arg2)
		return;
	Value sqlData = ParseJSON(arg1->c_str());
	Value payload = ParseJSON(*arg2);
	if(sqlData.IsError())
		return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_LOGIN, false, (uint32_t)userlistenProto::LoginResult::SYSTEM_ERROR, "登录失败，请稍后再试", userlistenProto::LOGIN_RES());
	uint32_t code = 0;
	const char* msg = nullptr;
	if (!MapAuthCode(sqlData["res_code"], !sqlData.IsNull() && sqlData.GetCount() > 0, code, msg))
	{
		int log_result = 2;
		int res_code = sqlData["res_code"].IsNull() ? -1 : (int)sqlData["res_code"];
		if (res_code == 5)      log_result = 4;
		else if (res_code == 4) log_result = 5;
		else if (res_code < 0)  log_result = 7;
		WriteLoginLog(sqlData["uid"].IsNull() ? 0 : (int64)sqlData["uid"], sqlData["username"].IsNull() ? "" : ~sqlData["username"], log_result, msg);
	}
	String phc = sqlData["cred_hash"];
	if (phc.IsEmpty())
		return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_LOGIN, false, (uint32_t)userlistenProto::LoginResult::SYSTEM_ERROR, "凭证数据异常", userlistenProto::LOGIN_RES());
	CredentialWorker::Instance().AsyncVerify(payload["cred_val"], phc,
		[this, sqlData, payload](bool ok, bool need_upgrade) {
			int64 now = Upp::GetUTCSeconds(Upp::GetSysTime());
			if (!ok)
			{
				assetEventBus.Emit("mysql_query_null", "", "UPDATE account_credential SET fail_count = fail_count + 1, last_fail_time = ? "
					"WHERE uid = ? AND cred_type = ?",
					WithDeepCopy< Vector<Value>>(Vector<Value>{ now, sqlData["uid"], payload["cred_type"] }));
				assetEventBus.Emit("mysql_query_null", "", "UPDATE account_credential SET locked_until = ? "
					"WHERE uid = ? AND cred_type = ? AND fail_count >= ?",
					WithDeepCopy< Vector<Value>>(Vector<Value>{ now + (int64)g_cfg.login_fail_lock, sqlData["uid"], payload["cred_type"], g_cfg.login_fail_limit }));
				WriteLoginLog(sqlData["uid"].IsNull() ? 0 : (int64)sqlData["uid"], sqlData["username"].IsNull() ? "" : ~sqlData["username"], 2, "密码错误");
				return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_LOGIN, false, (uint32_t)userlistenProto::LoginResult::PASSWORD_INCORRECT, "用户名不存在或密码错误", userlistenProto::LOGIN_RES());
			}
			assetEventBus.Emit("mysql_query_null", "", "UPDATE account_credential SET fail_count = 0, locked_until = NULL, last_fail_time = NULL "
				"WHERE uid = ? AND cred_type = ?",
				WithDeepCopy< Vector<Value>>(Vector<Value>{ sqlData["uid"], payload["cred_type"] }));
			assetEventBus.Emit("mysql_query_null", "", "UPDATE account SET last_login_time = ? WHERE uid = ?", WithDeepCopy< Vector<Value>>(Vector<Value>{ Upp::GetUTCSeconds(Upp::GetSysTime()), sqlData["uid"] }));
			int64 expire_at = now + token_ttl_sec_;
			client_map_.UpdateAs<PlayerInfo>(ScanUuid(~payload["sid"]), [this, &sqlData, &payload, expire_at](PlayerInfo& info) {
				String ticket;
				if (ticket_issuer_.HasKey(TicketType::LOGIN))
					ticket = ticket_issuer_.Issue(TicketType::LOGIN, (int64)sqlData["uid"], 0, 0, g_cfg.session_expire_sec);
				else
					sharedFeatures::PrintMsg("[Login] uid=%d 登录成功，但 LOGIN 票据密钥未配置，返回空 ticket", (int)sqlData["uid"]);
				String token = sharedFeatures::GenerateAdminToken();
				if (token.IsEmpty())
					return HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_LOGIN, false, (uint32_t)userlistenProto::LoginResult::SYSTEM_ERROR, "生成管理员令牌失败", userlistenProto::LOGIN_RES());
				info.token_session.token = token;
				info.token_session.issued_at = Upp::GetUTCSeconds(Upp::GetSysTime());
				info.token_session.last_seen_at = info.token_session.issued_at;
				info.token_session.expire_at = expire_at;
				info.uid = (int64)sqlData["uid"];
				LOGIN_RES res;
				res.uid = (int64)sqlData["uid"];
				res.ticket = ticket.ToStd();
				res.token = token;
				res.token_expire_at = expire_at;
				WriteLoginLog(sqlData["uid"].IsNull() ? 0 : (int64)sqlData["uid"], sqlData["username"].IsNull() ? "" : ~sqlData["username"], 1, "");
				HandleResponse(payload["sid"], payload["ackid"], userlistenProto::EVT::RES_LOGIN, true, (uint32_t)userlistenProto::LoginResult::OK, "登录成功", res);
				});
		});
}
void AccountServer::userlisten::HandleReconnectRequest(const userlistenProto::RECONNECT_REQ& args, const String& sid, int ackid)
{
	if (args.proto_ver != 1 || args.uid == 0 || args.token.empty())
		return HandleResponse(sid, ackid, userlistenProto::EVT::RES_RECONNECT, false, (uint32_t)userlistenProto::ReconnectResult::INVALID_PARAM, GetReconnectResultMessage(userlistenProto::ReconnectResult::INVALID_PARAM), userlistenProto::RECONNECT_RES());
	Uuid old_sid;
	client_map_.ForEachAs<PlayerInfo>([&args, &old_sid](const Uuid& client_sid, const PlayerInfo& player) {
		if (player.uid == args.uid && player.token_session.token == String(args.token))
			old_sid = client_sid;
	});
	SharedPtr<PlayerInfo> old_info = nullptr;
	if (!old_sid.IsNullInstance()) {
		client_map_.TryGetCopyAs(old_sid, old_info);
	}
	if (!old_info)
		return HandleResponse(sid, ackid, userlistenProto::EVT::RES_RECONNECT, false, (uint32_t)userlistenProto::ReconnectResult::TOKEN_NOT_FOUND, GetReconnectResultMessage(userlistenProto::ReconnectResult::TOKEN_NOT_FOUND), userlistenProto::RECONNECT_RES());
	client_map_.UpdateAs<PlayerInfo>(ScanUuid(sid), [this, &old_info, &sid, ackid](PlayerInfo& info) {
		info.token_session = old_info->token_session;
		info.uid = old_info->uid;
		HandleResponse(sid, ackid, userlistenProto::EVT::RES_RECONNECT, true, (uint32_t)userlistenProto::ReconnectResult::OK, GetReconnectResultMessage(userlistenProto::ReconnectResult::OK), userlistenProto::RECONNECT_RES());
	});
}
void AccountServer::userlisten::HandleTokenHeartbeatRequest(const userlistenProto::TOKEN_HEARTBEAT_REQ& args, const String& sid, int ackid)
{
	using namespace userlistenProto;
	TOKEN_HEARTBEAT_RES res;
	if (args.proto_ver != 1 || args.uid == 0 || args.token.empty())
		return HandleResponse(sid, ackid, EVT::RES_TOKEN_HEARTBEAT, false, (uint32_t)TokenResult::INVALID_PARAM, GetTokenResultMessage(TokenResult::INVALID_PARAM), res);
	client_map_.UpdateAs<PlayerInfo>(ScanUuid(sid), [this, &args, &sid, ackid, &res](PlayerInfo& info) {
		if (info.token_session.token != String(args.token) || info.token_session.expire_at <= Upp::GetUTCSeconds(Upp::GetSysTime()))
		{
			HandleResponse(sid, ackid, EVT::RES_TOKEN_HEARTBEAT, false, (uint32_t)TokenResult::TOKEN_NOT_FOUND, GetTokenResultMessage(TokenResult::TOKEN_NOT_FOUND), res);
			return net_interface_->DisconnectClient(sid);
		}
		HandleResponse(sid, ackid, EVT::RES_TOKEN_HEARTBEAT, true, (uint32_t)TokenResult::OK, GetTokenResultMessage(TokenResult::OK), res);
	});
}

void AccountServer::userlisten::SweepTokenSessions(SharedPtr<TIMETASK_BASE> task)
{
	Vector<Uuid> kick_sids;
	int64 now = Upp::GetUTCSeconds(Upp::GetSysTime());
	client_map_.ForEachAs<PlayerInfo>([&kick_sids, now](const Uuid& sid, const PlayerInfo& player) {
		if (!player.token_session.token.IsEmpty() && player.token_session.expire_at <= now)
			kick_sids.push_back(sid);
	});
	for (auto t : kick_sids)
	{
		client_map_.Erase(t);
		net_interface_->DisconnectClient(t.ToStringWithDashes());
	}
}

void AccountServer::userlisten::WriteLoginLog(int64 uid, const char* name, int result, const char* reason)
{
	int64 now = Upp::GetUTCSeconds(Upp::GetSysTime());
	assetEventBus.Emit("mysql_query_null", "WriteLoginLog", "INSERT INTO account_login_log (uid, username_in, result, fail_reason, login_time) "
		"VALUES (?, ?, ?, ?, ?)",
		WithDeepCopy<Vector<Value>>(Vector<Value>{ uid > 0 ? Value(uid) : Value(), String(name ? name : ""), result, String(reason ? reason : ""), now }));
}

bool AccountServer::userlisten::BasicVerification(NetLibrary::EMIEVENT evt, uint16_t proto_ver, const std::string& username,
	const std::string& cred_val, uint8_t cred_type, RES_BASE& res)
{
	using namespace userlistenProto;
	if (proto_ver != 1)
	{
		res.set(evt, false, (uint32_t)LoginResult::INVALID_PARAM, "协议版本不支持");
		return false;
	}
	if (!sharedFeatures::IsValidUsername(username.c_str()))
	{
		res.set(evt, false, (uint32_t)LoginResult::INVALID_PARAM, "用户名不合法");
		return false;
	}
	if (!sharedFeatures::IsValidCredType(cred_type))
	{
		res.set(evt, false, (uint32_t)LoginResult::CRED_TYPE_INVALID, "凭证类型不合法");
		return false;
	}
	if (cred_type == 1 && (cred_val.size() < 8 || cred_val.size() > 128))
	{
		res.set(evt, false, (uint32_t)LoginResult::INVALID_PARAM, "密码长度不合法(需在 8-128 之间)");
		return false;
	}
	return true;
}
