#ifndef _NetLibrary_ServerInterface_h_
#define _NetLibrary_ServerInterface_h_
#pragma once
#if defined(_WIN32)
#  if defined(NETLIB_EXPORTS)  // 构建库时在目标添加该宏
#    define NETLIB_API __declspec(dllexport)
#  else
#    define NETLIB_API __declspec(dllimport)
#  endif
#else
#  define NETLIB_API
#endif

namespace NetLibrary {
	class NETLIB_API NetInterface
	{
	public:
		virtual ~NetInterface() {};
		virtual void On(EMIEVENT ev, AssetLib::EventHandler&& fun) = 0;//注册事件
		virtual void Emit(EMIEVENT ev, AssetLib::BinaryData data, bool isBinary = false) = 0;//发送事件
		virtual void StartNetSver(const char* host, int port, int unum, size_t memsize) = 0;
		virtual void StartNetSver(const char* conf) = 0;
		virtual void ShutDownNetSver() = 0;
		virtual void DisconnectClient(const char* sid) = 0;
		// 便捷模板：注册对象成员函数（无返回值）
		template <typename T, typename... Args>
		void On(EMIEVENT ev, T* instance, void (T::* method)(Args...)) {
			AssetLib::EventHandler handler = [instance, method](const AssetLib::EventArgs& args) {
				// 示例：两个参数 const char*, const char*
				if constexpr (sizeof...(Args) == 2) {
					using A0 = std::tuple_element_t<0, std::tuple<Args...>>;
					using A1 = std::tuple_element_t<1, std::tuple<Args...>>;
					if constexpr (std::is_same_v<A0, const char*> && std::is_same_v<A1, const char*>) {
						const char* a0 = AssetLib::EventBus<>::ArgAs<const char*>(args, 0) ? *AssetLib::EventBus<>::ArgAs<const char*>(args, 0) : nullptr;
						const char* a1 = AssetLib::EventBus<>::ArgAs<const char*>(args, 1) ? *AssetLib::EventBus<>::ArgAs<const char*>(args, 1) : nullptr;
						(instance->*method)(a0, a1);
						return;
					}
				}
				// 默认：如果方法就是接受 EventArgs
				if constexpr (std::is_same_v<std::tuple_element_t<0, std::tuple<Args...>>, const AssetLib::EventArgs&> && sizeof...(Args) == 1) {
					(instance->*method)(args);
				}
			};
			On(ev, std::move(handler));
		}

		// 替换现有模板为基于 EventArgs 的桥接
		template <typename T>
		void On(EMIEVENT ev, T* instance, void (T::* method)(const AssetLib::EventArgs&)) {
			AssetLib::EventHandler handler = [instance, method](const AssetLib::EventArgs& args) {
				(instance->*method)(args);
			};
			On(ev, std::move(handler));
		}

		// 通用桥接：如果方法接受 BinaryData
		template <typename T>
		void On(EMIEVENT ev, T* instance, void (T::* method)(const AssetLib::BinaryData&)) {
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
		void On(EMIEVENT ev, Callable&& callable) {
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
	NETLIB_API void InitUppLogging(const char* logfile, bool enableConsole);
	NETLIB_API NetInterface* CreateNetLibrary(bool isserve);
};
#endif
