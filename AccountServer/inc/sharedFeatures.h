#pragma once
namespace AccountServer {
	/// <summary>
	/// 共享功能
	/// </summary>
	namespace sharedFeatures
	{
		/// <summary>
		/// 验证服务器配置是否合法
		/// </summary>
		/// <param name="svrconf">服务器配置 JSON 对象</param>
		/// <returns>合法返回 true，否则返回 false</returns>
		bool IsValidSvrconf(const NetLibrary::njson& svrconf);
		/// <summary>
		/// 验证 MySQL 配置是否合法
		/// </summary>
		/// <param name="mysqlconf">MySQL 配置 JSON 对象</param>
		/// <returns>合法返回 true，否则返回 false</returns>
		bool IsValidMysqlconf(const NetLibrary::njson& mysqlconf);
		/// <summary>
		/// 验证用户名是否合法
		/// </summary>
		/// <param name="s">用户名字符串</param>
		/// <returns>合法返回 true，否则返回 false</returns>
		bool IsValidUsername(String s);
		/// <summary>
		/// 验证凭证类型是否合法
		/// </summary>
		/// <param name="t">凭证类型</param>
		/// <returns>合法返回 true，否则返回 false</returns>
		bool IsValidCredType(uint8_t t);
		/// <summary>
		/// 生成管理员令牌
		/// </summary>
		/// <returns>管理员令牌字符串</returns>
		String GenerateAdminToken();
		/// <summary>
		/// 生成随机字符串
		/// </summary>
		/// <param name="length">字符串长度</param>
		/// <returns>随机字符串</returns>
		String GenerateRandomString(size_t length);

		template <typename... Args>
		void PrintMsg(const char* s, const Args&... args)
		{
			String msg;
			if constexpr (sizeof...(Args) == 0)
				msg = s ? s : "";
			else
				msg = Format(s, args...);
			Cout() << msg << "\n";
			RLOG(msg);
		}
	};
}