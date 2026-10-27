#pragma once

namespace ApiLibrary {

	/// <summary>
	/// 脚本服务抽象接口（供业务层依赖）
	/// </summary>
	class APILIB_API IScriptService {
	public:
		virtual ~IScriptService() = default;

		/// <summary>
		/// 启动脚本服务（创建并启动运行时）
		/// </summary>
		virtual bool Start(const ScriptRuntimeConfig& cfg, std::string* err = nullptr) = 0;

		/// <summary>
		/// 停止脚本服务并释放资源
		/// </summary>
		virtual void Stop() = 0;

		/// <summary>
		/// 投递脚本调用请求
		/// </summary>
		virtual void PostInvoke(const ScriptInvokeRequest& req) = 0;

		/// <summary>
		/// 投递脚本回调事件（用于异步数据库结果回流）
		/// </summary>
		virtual void PostCallback(const std::string& sid, const std::string& callback_ev, const AssetLib::BinaryData& payload) = 0;

		/// <summary>
		/// 热重载脚本
		/// </summary>
		virtual bool Reload(const std::string& entry_file, std::string* err = nullptr) = 0;

		/// <summary>
		/// 获取当前Worker数量
		/// </summary>
		virtual int GetWorkerCount() const = 0;
	};
}
