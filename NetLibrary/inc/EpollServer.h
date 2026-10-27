#ifndef _NetLibrary_EpollServer_h_
#define _NetLibrary_EpollServer_h_
namespace NetLibrary {
	class EpollServer : public Thread
	{
		typedef EpollServer CLASSNAME;
	public:
		Event<MsgType, MsgCode> WhenMsg;
		Event<MsgType, int> WhenMsgSys;
		Event<struct epoll_event&> handle_event;
	private:
		SockType stype;
#if defined(PLATFORM_POSIX)
		EPOLL epollFd = -1;
		EPOLL exit_fd = -1;//退出唤醒 (eventfd)
#else
		AssetLib::ThreadWorkPool<void> window_event_task;
		// Windows: use IOCP handle
		EPOLL epollFd = nullptr; // alias for IOCP HANDLE
		EPOLL error_fd = nullptr; // event handle (CreateEvent) - not used by epoll
		Thread iocp_daemon_thread;
#endif
		std::atomic<bool> isstart;
		const size_t INITIAL_BATCH_SIZE = 1024;
        const size_t MAX_BATCH_SIZE = 16384;  // 合理上限
        const size_t MIN_BATCH_SIZE = 64;
#if defined(PLATFORM_POSIX)
		alignas(64) Vector<struct epoll_event> event_batch;
#else
		alignas(64) Vector<OVERLAPPED_ENTRY> event_batch;
#endif
		size_t current_batch_size;		
		struct {
			size_t total_events;
			size_t max_events_per_batch;
			size_t batches_processed;
			time_t last_stats_time;
		} stats;
		//struct epoll_event serverev;
		Mutex lock;
	private:
		void Loop();

		// Windows IOCP helpers
#ifdef DEBUG

#endif // DEBUG

		void LoopIOCP();
		bool CreateIOCP();
		void update_stats(int ready_count);
		void adjust_batch_size();
		void log_stats();
#ifdef PLATFORM_WIN32
		/// <summary>
		/// 启动 IOCP（I/O 完成端口）守护进程，在后台处理异步 I/O 事件。
		/// </summary>
		void StartIocpDaemon();
#endif
	public:
		EpollServer();
		EpollServer(SockType s);
		~EpollServer() = default;
		Event<> EpollErrorWhenStop;//epoll退出事件
		void StopEpoll();
		bool Init();
		void RunLoop();
		bool set_nonblocking(SOCKET fd);
		bool SetEpoll(SOCKET fd, uint32_t events, int type);
		bool GetIsStart() {
			Mutex::Lock __(lock);
			return isstart;
		}
	
	};
};
#endif
