#pragma once
// ============================================================================
// ServerConfig.h —— 全局配置的内存快照
//
// 用途：global_config 表只在启动时读一次（以及热加载时刷新），
//       登录/注册等热路径上不再查库。
//
// ★ 注意：表里 cfg_value 是 varchar(2048) 而不是整数，读取必须做类型转换。
// ============================================================================

namespace AccountServer {
	/// <summary>
	/// 资产事件总线
	/// </summary>
	extern AssetLib::EventBus<> assetEventBus;

	template<class T>
	using SharedPtr = std::shared_ptr<T>;

	template<class T, class... Args>
	inline std::shared_ptr<T> MakeShared(Args&&... args) {
		return std::make_shared<T>(std::forward<Args>(args)...);
	};
	struct ServerConfig
	{
		// 开关
		bool    login_enabled = true;    // 维护期可一键关闭登录
		bool    register_enabled = true;    // 维护期可一键关闭注册

		// 安全
		int     login_fail_limit = 5;       // 连续失败达到此次数后锁定
		int     login_fail_lock = 900;     // 锁定时长（秒）

		// 会话
		int     session_expire_sec = 300;     // 票据有效期（秒）
		int     session_protect_window = 60;      // 断线保护窗口（秒）
		int     refresh_token_ttl = 604800;  // 长效凭据 7 天

		// 业务
		int     max_role_per_zone = 3;// 每个区最大角色数

		// 运营
		String  client_min_version;// 客户端最低版本号
		String  client_update_url;// 客户端更新地址
		String  notice_login;// 登录页公告
		String  notice_world;// 大厅页公告

		void Set(const String& key, const String& val);
	};

	inline void ServerConfig::Set(const String& key, const String& val)
	{
		if (key == "login_enabled")          login_enabled = (val != "0");
		else if (key == "register_enabled")       register_enabled = (val != "0");
		else if (key == "login_fail_limit")       login_fail_limit = StrInt(val);
		else if (key == "login_fail_lock")        login_fail_lock = StrInt(val);
		else if (key == "session_expire_sec")     session_expire_sec = StrInt(val);
		else if (key == "session_protect_window") session_protect_window = StrInt(val);
		else if (key == "refresh_token_ttl")      refresh_token_ttl = StrInt(val);
		else if (key == "max_role_per_zone")      max_role_per_zone = StrInt(val);
		else if (key == "client_min_version")     client_min_version = val;
		else if (key == "client_update_url")      client_update_url = val;
		else if (key == "notice_login")           notice_login = val;
		else if (key == "notice_world")           notice_world = val;
	}

	// ★ 全局唯一实例。定义在 src/ServerConfig.cpp。
	//   热路径只读它，不查库；启动时（或热加载时）由 LoadGlobalConfig 填充。
	extern ServerConfig g_cfg;

	// 从库里加载。调用方需提供 SqlExecEv（查询回调）。
	// zone_scope IS NULL 表示全局配置，区级覆盖配置暂不读取。
	inline void LoadGlobalConfig(const AssetLib::EventArgs& args)
	{
		const std::string* arg1 = AssetLib::EventBus<>::ArgAs<const std::string>(args, 0);
		Value sqlData = ParseJSON(arg1->c_str());
		if (sqlData.IsError() || sqlData.IsNull())
			return;
		for (int i = 0; i < sqlData.GetCount(); ++i)
		{
			Value row = sqlData[i];
			if (!row.Is<ValueMap>())
				continue;
			String key = row["cfg_key"];
			String val = row["cfg_value"];
			g_cfg.Set(key, val);
		}
	}

}
