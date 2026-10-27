#pragma once


namespace ApiLibrary {

	/// <summary>
	/// 脚本执行结果码
	/// </summary>
	enum class ScriptResultCode : int {
		OK = 0,
		TIMEOUT = 1,
		OP_LIMIT = 2,
		SCRIPT_ERROR = 3,
		ROUTE_ERROR = 4,
		RUNTIME_DOWN = 5,
	};

	/// <summary>
	/// 脚本调用请求
	/// </summary>
	struct ScriptInvokeRequest {
		std::string sid;                // 会话ID（用于一致性路由）
		std::string event_name;         // 要执行的脚本事件名
		AssetLib::BinaryData payload;   // 脚本输入载荷（通常为JSON）
		int timeout_ms = 200;           // 超时预算（毫秒）
		int op_limit = 200000;          // 脚本执行步数限制
		std::string trace_id;           // 跟踪ID（日志关联）
	};

	/// <summary>
	/// 脚本调用结果
	/// </summary>
	struct ScriptInvokeResult {
		ScriptResultCode code = ScriptResultCode::OK;
		std::string message;
		std::string trace_id;
		int worker_id = -1;
		int64_t elapsed_ms = 0;
	};

	/// <summary>
	/// 脚本运行时配置
	/// </summary>
	struct ScriptRuntimeConfig {
		std::string script_root;                   // 脚本目录
		std::string entry_file;                    // 可选：入口脚本文件
		int worker_count = 1;                      // Worker数量（当前实现建议为1）
		int default_timeout_ms = 200;              // 默认超时毫秒
		int default_op_limit = 200000;             // 默认步数限制
		bool enable_hot_reload = true;             // 是否允许热重载
		std::unordered_map<std::string, AssetLib::EventHandler> custom_event_handlers; // 自定义事件处理器
		AssetLib::EventHandler on_api_execute;     // 脚本触发 apiexecute 时回调到宿主
	};
}
