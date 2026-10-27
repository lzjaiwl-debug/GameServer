#pragma once

namespace ApiLibrary {

	/// <summary>
	/// 脚本服务门面：
	/// 对外提供统一接口，内部委托 ScriptDispatcher。
	/// </summary>
	class APILIB_API ScriptService : public IScriptService {
		typedef ScriptService CLASSNAME;
	public:
		ScriptService();
		~ScriptService() override;

		/// <summary>
		/// 启动脚本服务
		/// </summary>
		bool Start(const ScriptRuntimeConfig& cfg, std::string* err = nullptr) override;

		/// <summary>
		/// 停止脚本服务
		/// </summary>
		void Stop() override;

		/// <summary>
		/// 投递脚本调用
		/// </summary>
		void PostInvoke(const ScriptInvokeRequest& req) override;

		/// <summary>
		/// 投递脚本回调
		/// </summary>
		void PostCallback(const std::string& sid, const std::string& callback_ev, const AssetLib::BinaryData& payload) override;

		/// <summary>
		/// 热重载脚本
		/// </summary>
		bool Reload(const std::string& entry_file, std::string* err = nullptr) override;

		/// <summary>
		/// 获取Worker数量
		/// </summary>
		int GetWorkerCount() const override;

	private:
		std::atomic<bool> started_{ false };
		std::unique_ptr<ScriptDispatcher> dispatcher_;
		ScriptRuntimeConfig cfg_;
	};
}
