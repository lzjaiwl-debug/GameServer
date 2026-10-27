#pragma once
#include <csignal>
namespace AccountServer {
	class server
	{
		typedef server CLASSNAME;
	public:
		server();
		~server() = default;
		void Start();
		void Stop();
	private:
		static std::atomic_bool g_running;
		static std::condition_variable g_cv;
		std::mutex g_mtx;
		AssetLib::TimeEventAsync<void, SharedPtr<TIMETASK_BASE>> timeAsync;
		NetLibrary::njson config;
		adminlisten adminlisten_handler;
		MysqlServer mysql;
		static void HandleSignal(int) {
			g_running.store(false, std::memory_order_relaxed);
			g_cv.notify_one();
		}
		void SetupSignalHandlers() {
			// 使用 std::signal，跨平台（Linux/Windows）可用；仅 SIGBREAK 需 _WIN32 保护
			std::signal(SIGINT, HandleSignal);
			std::signal(SIGTERM, HandleSignal);
#ifdef _WIN32
			std::signal(SIGBREAK, HandleSignal);
#endif
		}
		void WaitForShutdown() {
			std::unique_lock<std::mutex> lk(g_mtx);
			g_cv.wait(lk, [this] { return !g_running.load(std::memory_order_relaxed); });
		}
		void Start_Mysql();
		
	public:
		void StartCreateAdminWizardAsync();
	};
}