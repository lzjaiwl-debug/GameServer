#include "../inc/include.h"
EpollServer::EpollServer()
	: current_batch_size(INITIAL_BATCH_SIZE)
{
#if defined(PLATFORM_WIN32)
    window_event_task.Create(0, AssetLib::Event<void>{ this, &CLASSNAME::Loop });
#endif
}
NetLibrary::EpollServer::EpollServer(SockType s) : EpollServer()
{
	stype = s;
}
void EpollServer::update_stats(int ready_count)
{
	stats.total_events += ready_count;
	stats.batches_processed++;
	stats.max_events_per_batch = ::max(stats.max_events_per_batch, (size_t)ready_count);
}
void EpollServer::adjust_batch_size()
{
	time_t now = time(nullptr);

	// 每10秒调整一次
	if(now - stats.last_stats_time < 10)
		return;

	// 如果经常达到批次上限，考虑扩容
	double utilization_ratio = (double)stats.max_events_per_batch / current_batch_size;

	if(utilization_ratio > 0.8 && current_batch_size < MAX_BATCH_SIZE) {
		// 扩容，但不要超过最大值
		size_t new_size = std::min(current_batch_size * 2, MAX_BATCH_SIZE);
		event_batch.SetCount(new_size);
		current_batch_size = new_size;
        assetEventBus.Emit(SYSEVENT::SYSLOG, ~Format("adjust_batch_size: Increased batch size to %d (utilization:%d)", (int)new_size, (int)utilization_ratio));
	}
	else if(utilization_ratio < 0.3 && current_batch_size > MIN_BATCH_SIZE) {
		// 缩容
		size_t new_size = std::max(current_batch_size / 2, MIN_BATCH_SIZE);
		event_batch.SetCount(new_size);
		current_batch_size = new_size;
        assetEventBus.Emit(SYSEVENT::SYSLOG, ~Format("adjust_batch_size: Decreased batch size to %d (utilization:%d)", (int)new_size, (int)utilization_ratio));
	}
	// 重置统计
	stats.max_events_per_batch = 0;
	stats.last_stats_time = now;
}
void EpollServer::log_stats()
{
	static time_t last_log = 0;
	time_t now = time(nullptr);

	if(now - last_log >= 30) { // 每30秒输出一次统计
		double events_per_second = stats.total_events / 30.0;
        assetEventBus.Emit(SYSEVENT::SYSLOG, ~Format("print_events: Performance stats - Events/s:%d, Batch size:%d, Batches:%d", events_per_second, (int)current_batch_size, (int)stats.batches_processed));
		stats.total_events = 0;
		stats.batches_processed = 0;
		last_log = now;
	}
}

bool EpollServer::Init()
{
    assetEventBus.Emit(SYSEVENT::SYSLOG, "Initializing EpollServer...");
#if defined(PLATFORM_POSIX)
    if(epollFd != 0)
    {
        assetEventBus.Emit(SYSEVENT::SYSLOG, "Epoll already created.");
		return true;
    }
    if((epollFd = epoll_create1(EPOLL_CLOEXEC)) < 0)
    {
        assetEventBus.Emit(SYSEVENT::SYSERR, "Epoll already created.");
        throw NetException(ExcType::EPOLLERROR, errno, (int64)this, "Epoll create failed: %s", std::strerror(errno));
    }
    exit_fd = eventfd(0, EFD_NONBLOCK);
    if (exit_fd < 0)
    {
        assetEventBus.Emit(SYSEVENT::SYSERR, "Epoll already created.");
        throw NetException(ExcType::EPOLLERROR, errno, (int64)this, "Eventfd create failed: %s", std::strerror(errno));
    }
    SetEpoll(exit_fd, EPOLLIN, EPOLL_CTL_ADD);
#else
    // On Windows create IOCP
    if ((epollFd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0)) == NULL)
    {
        assetEventBus.Emit(SYSEVENT::SYSERR, "Create IOCP failed:" + GetLastError());
        throw NetException(ExcType::EPOLLERROR, GetLastError(), (int64)this,
            "Create IOCP failed: %d", GetLastError());
    }
    // Windows: use an event for Stop notification
    error_fd = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (!error_fd)
    {
        assetEventBus.Emit(SYSEVENT::SYSERR, "Create event for IOCP stop failed:" + GetLastError());
        throw NetException(ExcType::EPOLLERROR, GetLastError(), (int64)this,
            "Create event for IOCP stop failed: %d", GetLastError());
    }
    //SetEpoll((int)(uintptr_t)exit_fd, EPOLLIN, 0);
#endif
	event_batch.SetCount(current_batch_size);
	memset(&stats, 0, sizeof(stats));
	stats.last_stats_time = time(nullptr);
    assetEventBus.Emit(SYSEVENT::SYSLOG, "EpollServer initialized successfully.");
	return true;
}

void EpollServer::RunLoop()
{
    Mutex::Lock __(lock);
    if (isstart)
        return;
    isstart = true;
#if defined(PLATFORM_WIN32)
    window_event_task.StartAll();
    iocp_daemon_thread.Run([this] { StartIocpDaemon(); });
#else
    // Always run Loop() which contains platform branches
    this->Run([this]{ Loop(); });
#endif
}

void EpollServer::Loop()
{
#if defined(PLATFORM_POSIX)
    while (isstart) {
        int nfds = epoll_wait(epollFd, event_batch.Begin(), event_batch.GetCount(), -1);
        if (nfds < 0)
        {
            if (errno == EINTR)
            {
                assetEventBus.Emit(SYSEVENT::SYSLOG, "Epoll wait interrupted by signal, continuing...");
                NetException::PrintMsg("Epoll wait interrupted by signal, continuing...");
                continue; // 被信号中断，继续等待
            }
            else
            {
                assetEventBus.Emit(SYSEVENT::SYSLOG, "Epoll wait error, stopping...");
                NetException::PrintMsg("Error: Epoll wait error: %s", std::strerror(errno));
                Thread::Run([this]() {
                    EpollErrorWhenStop();
                });// 异步触发退出事件
                return;
            }
        }
        // 更新统计信息
        update_stats(nfds);
        // 8. 处理所有就绪的事件
        for (int i = 0; i < nfds; ++i) {
            if (event_batch[i].data.fd == exit_fd)
            {
                uint64_t u;
                read(exit_fd, &u, sizeof(uint64_t));
                isstart = false;
                break;
            }
            handle_event(event_batch[i]);
        }
        // 动态调整批次大小
        adjust_batch_size();
        // 定期输出统计信息
        log_stats();
    }
#else
    // Windows IOCP loop implementation
    int consecutiveErrors = 0;
    while (isstart) {
        DWORD numEntries = 0;
        BOOL ok = GetQueuedCompletionStatusEx((HANDLE)epollFd, event_batch.begin(), event_batch.GetCount(), &numEntries, INFINITE, FALSE);
        if (!ok) {
            DWORD errorCode = GetLastError();
            switch (errorCode)
            {
            case ERROR_INVALID_HANDLE:
            case ERROR_ABANDONED_WAIT_0:
                // The IOCP handle was closed
                assetEventBus.Emit(SYSEVENT::SYSLOG, "Error: IOCP handle closed, stopping epoll loop");
                isstart = false;
                //
                if (!SetEvent(error_fd))
                {
                    assetEventBus.Emit(SYSEVENT::SYSLOG, "Error: Failed to signal error event on IOCP stop %d" + GetLastError());
                    continue;
                }
                return;
            case WAIT_TIMEOUT:
            case ERROR_INVALID_PARAMETER:
            case ERROR_INSUFFICIENT_BUFFER:
                assetEventBus.Emit(SYSEVENT::SYSLOG, "GetQueuedCompletionStatusEx recoverable error:" + errorCode);
                continue;
            case ERROR_NOT_ENOUGH_MEMORY:
            case ERROR_NO_SYSTEM_RESOURCES:
                if (consecutiveErrors >= 10) {
                    // 连续错误太多，升级为致命
                    assetEventBus.Emit(SYSEVENT::SYSLOG, "Error: Too many consecutive fatal errors in IOCP, stopping epoll loop");
                    isstart = false;
                    if (!SetEvent(error_fd))
                    {
						assetEventBus.Emit(SYSEVENT::SYSLOG, "Error: Failed to signal error event on IOCP stop %d" + GetLastError());
                        continue;
                    }
                    return;
                }
                // 错误冷却
                Thread::Sleep(10000);
                continue;
            default:
				assetEventBus.Emit(SYSEVENT::SYSLOG, "Error: GetQueuedCompletionStatusEx fatal error: %d", errorCode);
                isstart = false;
                if (!SetEvent(error_fd))
                {
					assetEventBus.Emit(SYSEVENT::SYSLOG, "Error: Failed to signal error event on IOCP stop %d" + GetLastError());
                    continue;
                }
                return;
            }
        }
        update_stats((int)numEntries);
        for (DWORD i = 0; i < numEntries; ++i) {
            // convert OVERLAPPED_ENTRY to epoll_event-like stub
            struct epoll_event ev;
            ZeroMemory(&ev, sizeof(ev));
            // expose overlapped pointer for special completions (AcceptEx/Recv)
            // 检测停止哨兵
            if (event_batch[i].lpCompletionKey == (ULONG_PTR)-1 &&
                event_batch[i].dwNumberOfBytesTransferred == (DWORD)-1) {
                assetEventBus.Emit(SYSEVENT::SYSLOG, "IOCP stop signal received, exiting loop");
                return;
            }
            ev.data.fd = (SOCKET)(ULONG_PTR)event_batch[i].lpCompletionKey;
            // if overlapped points to our IocpContext (OVERLAPPED is first member),
            // record transferred bytes and event flags
            if (event_batch[i].lpOverlapped) {
                // 从 OVERLAPPED* 计算 IocpContext* （容器对象指针）
                auto* ol = event_batch[i].lpOverlapped;
                IocpContext* ctx = reinterpret_cast<IocpContext*>(
                    reinterpret_cast<char*>(ol) - offsetof(IocpContext, ov));

                ctx->bytesTransferred = event_batch[i].dwNumberOfBytesTransferred;

                DWORD internalErr = event_batch[i].Internal;
                if (internalErr != 0) {
                    ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
                }
                else {
                    // 区分接收/发送
                    if (ctx->events == EPOLLIN) {
                        ev.events = (ctx->bytesTransferred == 0) ? (EPOLLRDHUP) : EPOLLIN;
                    }
                    else if (ctx->events == EPOLLOUT) {
                        // 对发送，0字节不一定是错误，保持EPOLLOUT让上层决定
                        ev.events = EPOLLOUT;
                    }
                    else if (ctx->events == EPOLLCONNET) {
                        ev.events = EPOLLCONNET;
                    }
                    else {
                        ev.events = ctx->events;
                    }
                }
                ev.data.ptr = ctx;
            }
            else {
                // No overlapped pointer -> this may represent an error/completion
                // with no user context. Translate common IOCP error conditions
                // to epoll-like flags so upper layers receive consistent signals.
                DWORD err = event_batch[i].Internal;
                if (err != 0) {
                    // Map IOCP failure to EPOLLERR | EPOLLHUP
                    ev.events = EPOLLERR | EPOLLHUP | EPOLLRDHUP;
                }
                else {
                    // no error and no overlapped -> treat as readable
                    ev.events = EPOLLIN;
                }
            }
            handle_event(ev);
        }
        adjust_batch_size();
        log_stats();
    }
#endif
}
#ifdef PLATFORM_WIN32
void NetLibrary::EpollServer::StartIocpDaemon()
{
    while (isstart)
    {
        DWORD dwWaitResult = WaitForSingleObject(error_fd, INFINITE);
        switch (dwWaitResult)
        {
        case WAIT_OBJECT_0:
            // The event was signaled, exit the loop
			assetEventBus.Emit(SYSEVENT::SYSLOG, "IOCP daemon received stop signal, exiting...");
            continue;
        case WAIT_FAILED:
			assetEventBus.Emit(SYSEVENT::SYSERR, "Error: WaitForSingleObject failed in IOCP daemon:" + (int)GetLastError());
            EpollErrorWhenStop();
            return;
        }
    }
}
#endif
void EpollServer::StopEpoll()
{
    Mutex::Lock __(lock);
    if (!isstart)
        return;
	assetEventBus.Emit(SYSEVENT::SYSLOG, "Stopping EpollServer...");
	isstart = false;
#if defined(PLATFORM_POSIX)
    uint64_t u = 1;
    write(exit_fd, &u, sizeof(uint64_t));
    this->Wait();
	SetEpoll(exit_fd, 0, EPOLL_CTL_DEL);
#else
	SetEvent(error_fd);// signal the error event to stop loop
    if(iocp_daemon_thread.IsOpen())
        iocp_daemon_thread.Wait();// wait for IOCP daemon to exit
    if (window_event_task.GetActiveThreadCount() > 0)
    {
        for (int i = 0; i < window_event_task.GetActiveThreadCount(); i++)
        {
            PostQueuedCompletionStatus((HANDLE)epollFd, -1, -1, NULL);
        }
        window_event_task.StopAll();
    }
    // signal the Windows event to stop loop
    //SetEvent((HANDLE)exit_fd);
    if(epollFd) { CloseHandle(epollFd); epollFd = nullptr; }
    if(error_fd) { CloseHandle(error_fd); error_fd = nullptr; }
#endif
    current_batch_size = INITIAL_BATCH_SIZE;
    event_batch.clear();
	assetEventBus.Emit(SYSEVENT::SYSLOG, "EpollServer stopped.");
}
bool EpollServer::set_nonblocking(SOCKET fd)
{
#if defined(PLATFORM_POSIX)
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        assetEventBus.Emit(SYSEVENT::SYSERR, ~Format("Failed to set non-blocking mode for fd %d: %s", (int)fd, std::strerror(errno)));
        return false;
    }
#else
    u_long v = 1;
    if(ioctlsocket((SOCKET)fd, FIONBIO, &v) != 0)
    {
		assetEventBus.Emit(SYSEVENT::SYSERR, ~Format("Failed to set non-blocking mode for socket %d: %d", (int)fd, (int)GetLastError()));
		return false;
    }
#endif
	return true;
}
bool EpollServer::SetEpoll(SOCKET fd, uint32_t events, int type)
{
	Mutex::Lock __(lock);
	// 11. 将客户端socket添加到epoll监听
#if defined(PLATFORM_POSIX)
    struct epoll_event ev;
    ev.events = events;//EPOLLIN | EPOLLET | EPOLLRDHUP | EPOLLONESHOT;  // 边缘触发模式，提高性能
    ev.data.fd = fd;
    if(epoll_ctl(epollFd, type, fd, events == 0 ? NULL : &ev) < 0)
    {
        assetEventBus.Emit(SYSEVENT::SYSERR, "epoll_ctl failed error: " + IntStr(errno));
        return false;
    }
#else
    // Windows: associate socket with IOCP
    HANDLE h = CreateIoCompletionPort((HANDLE)(uintptr_t)fd, (HANDLE)epollFd, (ULONG_PTR)fd, 0);
    if(h == NULL)
    {
        assetEventBus.Emit(SYSEVENT::SYSERR, "CreateIoCompletionPort failed error: " + IntStr(GetLastError()));
        return false;
    }
#endif
	assetEventBus.Emit(SYSEVENT::SYSLOG, ~Format("Set fd (%d) to epoll monitoring", (int)fd));
	return true;
}

#if defined(PLATFORM_WIN32)
bool EpollServer::CreateIOCP()
{
    epollFd = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    return epollFd != NULL;
}

void EpollServer::LoopIOCP()
{
    // This function should typically be similar to Loop() but using GetQueuedCompletionStatusEx
    // Implementation provided in Loop() under PLATFORM_POSIX/PLATFORM_WIN32 branches.
}
#endif

