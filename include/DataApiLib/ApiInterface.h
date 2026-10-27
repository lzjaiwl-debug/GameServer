#pragma once

#if defined(_WIN32)
#  if defined(APILIB_EXPORTS)  // 构建库时在目标添加该宏
#    define APILIB_API __declspec(dllexport)
#  else
#    define APILIB_API __declspec(dllimport)
#  endif
#else
#  define APILIB_API
#endif

namespace ApiLibrary {
	// 助手：安全获取参数（类型检查）
	template <typename T>
	using ArgStore = std::remove_cv_t<std::remove_reference_t<T>>;

	template <typename T>
	inline static const ArgStore<T>* ArgAs(const AssetLib::EventArgs& args, size_t index)
	{
		if (index >= args.size()) return nullptr;
		const std::any& a = args[index];
		return std::any_cast<ArgStore<T>>(&a);
	}

	template <typename T>
	inline static ArgStore<T> ArgValue(const AssetLib::EventArgs& args, size_t index)
	{
		if (const auto* p = ArgAs<T>(args, index))
			return *p;
		return ArgStore<T>{};
	}
	class APILIB_API ApiInterface
	{
	public:
		virtual ~ApiInterface() {};
		virtual void On(std::string ev, AssetLib::EventHandler&& fun) = 0;//注册事件
		virtual void Emit(std::string ev, AssetLib::BinaryData data) = 0;//发送事件
		virtual void UploadData(const char* id, AssetLib::BinaryData data) = 0;//上传数据
		virtual void SetScriptPath(const char* path) = 0;//设置脚本路径
		virtual void LoadScriptFile(const char* scriptfile) = 0;//加载脚本文件
		virtual void ResetScriptEnvironment() = 0;//重置脚本环境
		virtual void FreeUpResources() = 0;//释放资源
		virtual nlohmann::json GetScriptFunctions(const char* scriptns) = 0;//获取命名空间下的函数列表
		virtual void RegExtFun(std::string ns, std::string ev, AssetLib::EventHandlerR&& fun) = 0;//注册扩展函数到指定命名空间
		virtual void RegExtFun(std::string ns, std::string ev, AssetLib::EventHandler&& fun) = 0;//注册扩展函数到指定命名空间
		template <typename T, typename R, typename... Args, size_t... I>
		R CallWithArgs(T* instance, R (T::* method)(Args...), const AssetLib::EventArgs& args, std::index_sequence<I...>) {
			return (instance->*method)(ArgValue<Args>(args, I)...);
		}
		template <typename T, typename R, typename... Args>
		void RegExtFun(std::string ns, std::string ev, T* instance, R (T::* method)(Args...))
		{
			if constexpr (std::is_void_v<R>) {
				AssetLib::EventHandler h = [=](const AssetLib::EventArgs& args) {
					if (args.size() < sizeof...(Args)) return;
					CallWithArgs(instance, method, args, std::index_sequence_for<Args...>{});
				};
				RegExtFun(ns, ev, std::move(h));
			}
			else {
				AssetLib::EventHandlerR h = [=](const AssetLib::EventArgs& args) -> std::any {
					if (args.size() < sizeof...(Args)) return {};
					return CallWithArgs(instance, method, args, std::index_sequence_for<Args...>{});
				};
				RegExtFun(ns, ev, std::move(h));
			}
		}
		// 便捷模板：注册对象成员函数（无返回值）
		template <typename T, typename... Args>
		void On(std::string ev, T* instance, void (T::* method)(Args...)) {
			AssetLib::EventHandler handler = [instance, method](const AssetLib::EventArgs& args) {
				// 示例：两个参数 const char*, const char*
				if (args.size() < sizeof...(Args)) return;
				CallWithArgs(instance, method, args, std::index_sequence_for<Args...>{});
			};
			On(ev, std::move(handler));
		}

		// 替换现有模板为基于 EventArgs 的桥接
		template <typename T>
		void On(std::string ev, T* instance, void (T::* method)(const AssetLib::EventArgs&)) {
			AssetLib::EventHandler handler = [instance, method](const AssetLib::EventArgs& args) {
				(instance->*method)(args);
			};
			On(ev, std::move(handler));
		}

		// 通用桥接：如果方法接受 BinaryData
		template <typename T>
		void On(std::string ev, T* instance, void (T::* method)(const AssetLib::BinaryData&)) {
			AssetLib::EventHandler handler = [instance, method](const AssetLib::EventArgs& args) {
				if (args.size() >= 1) {
					if (auto p = std::any_cast<const AssetLib::BinaryData>(&args[0])) {
						(instance->*method)(*p);
					}
				}
			};
			On(ev, std::move(handler));
		}

		// 便捷模板：注册任意可调用对象（如 lambda、std::function）
		template <typename Callable>
		void On(std::string ev, Callable&& callable) {
			AssetLib::EventHandler handler = [fn = std::forward<Callable>(callable)](std::any payload) {
				// 若 callable 接受 std::any
				if constexpr (std::is_invocable_v<Callable, std::any>) {
					fn(payload);
				}
				// 若 callable 接受 AssetLib::BinaryData
				else if (payload.type() == typeid(AssetLib::BinaryData)
					&& std::is_invocable_v<Callable, AssetLib::BinaryData>) {
					fn(std::any_cast<const AssetLib::BinaryData&>(payload));
				}
				else {
					// 无匹配签名时不调用或加入日志
				}
			};
			On(ev, std::move(handler));
		}
	};
	// 兼容旧代码：进程级单例接口（不建议用于多Worker脚本运行时）
	APILIB_API void DestroyApiInterface();
	using ApiInterfacePtr = ApiInterface*;
	APILIB_API void InitUppLogging(const char* logfile, bool enableConsole);
	APILIB_API ApiInterfacePtr CreateApiInterface();

	// 推荐：独立实例接口（每次返回新对象，适用于多Worker脚本服务）
	APILIB_API ApiInterfacePtr CreateApiInterfaceInstance();
	APILIB_API void DestroyApiInterfaceInstance(ApiInterfacePtr api);
};