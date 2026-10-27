#pragma once
// TimeEventAsync 支持自定义返回类型 R（默认 void），以及任意数量参数 TArgs...
// - 当 TArgs... 为空时，等价于周期性触发的 Event<R>() 回调
// - 当 TArgs... 非空时，以 tuple 存储 payload，调用时使用 std::apply 展开
// - PerformTasks 返回 R（void 时不返回值），仅在触发周期到达时执行回调

namespace AssetLib {

    template <typename R, typename... TArgs>
    struct SharedEvent {
        std::shared_ptr<Event<R, TArgs...>> fn;

        SharedEvent() = default;
        SharedEvent(Event<R, TArgs...>&& e) : fn(std::make_shared<Event<R, TArgs...>>(std::move(e))) {}

        explicit operator bool() const noexcept { return (bool)fn; }

        template <typename... Args>
        R operator()(Args&&... args) const {
            if constexpr (std::is_void_v<R>) {
                if (fn) (*fn)(std::forward<Args>(args)...);
            }
            else {
                if (fn) return (*fn)(std::forward<Args>(args)...);
                return R{};
            }
        }
    };

    // 非 void 的任务（支持返回类型 R）
    template <typename R = void, typename... TArgs>
    struct TimerTask {
        SharedEvent<R, TArgs...> fn;
        std::tuple<TArgs...> data{};
        int value = 0;  // 已累计的心跳数
        int cycle = -1; // 触发周期（心跳数），<=0 表示禁用

        TimerTask() = default;
        TimerTask(Event<R, TArgs...> cb, int c, std::tuple<TArgs...> d)
            : fn(std::move(cb)), data(std::move(d)), value(0), cycle(c) {
        }

        // 仅允许移动，禁止拷贝（Event 禁止拷贝）
        TimerTask(TimerTask&&) noexcept = default;
        TimerTask& operator=(TimerTask&&) noexcept = default;
        TimerTask(const TimerTask&) = delete;
        TimerTask& operator=(const TimerTask&) = delete;

        // 仅在触发时返回回调值；否则返回默认值（非 void 才需要）/不返回（void）
        R PerformTasks() {
            if (cycle <= 0) {
                if constexpr (!std::is_void_v<R>) return R{};
                else return;
            }
            if (++value >= cycle) {
                value = 0;
                try {
                    if (fn) {
                        if constexpr (sizeof...(TArgs) == 0) {
                            return fn();
                        } else {
                            return std::apply(fn, data);
                        }
                    }
                } catch (...) {
                    // swallow exceptions to keep timer thread alive
                }
                if constexpr (!std::is_void_v<R>) return R{};
            } else {
                if constexpr (!std::is_void_v<R>) return R{};
                else return;
            }
        }
    };

    template <typename R = void, typename... TArgs>
    class TimeEventAsync {
        typedef TimeEventAsync CLASSNAME;
        static constexpr bool kPayloadCopyable = std::conjunction_v<std::is_copy_constructible<std::decay_t<TArgs>>...>;

        static void InvokeCall(const SharedEvent<R, TArgs...>& fn, std::tuple<TArgs...>& data) {
            if constexpr (sizeof...(TArgs) == 0) {
                (void)fn();
            }
            else if constexpr (kPayloadCopyable) {
                (void)std::apply([&fn](auto&... xs) { (void)fn(xs...); }, data);
            }
            else {
                (void)std::apply([&fn](auto&... xs) { (void)fn(std::move(xs)...); }, data);
            }
        }
    public:
        TimeEventAsync() : running(false), interval_ms(0)
#ifdef _WIN32
            , hTimer(NULL), stopEvent(NULL)
#endif
        {
        }
        ~TimeEventAsync() { Stop(); }

        // 启动定时器（毫秒）
        void TimeRun(uint64_t milliseconds) {
            Mutex::Lock __(control_mutex);
            if (running) return;
            interval_ms = milliseconds;
#ifdef _WIN32
            stopEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (!stopEvent) return;

            hTimer = ::CreateWaitableTimer(NULL, FALSE, NULL);
            if (!hTimer) {
                CloseHandle(stopEvent);
                stopEvent = NULL;
                return;
            }
            LARGE_INTEGER liDueTime;
            liDueTime.QuadPart = -static_cast<LONGLONG>(milliseconds * 10000LL); // 负值表示相对时间
            if (!::SetWaitableTimer(hTimer, &liDueTime, static_cast<LONG>(milliseconds), NULL, NULL, FALSE)) {
                CloseHandle(hTimer); hTimer = NULL;
                CloseHandle(stopEvent); stopEvent = NULL;
                return;
            }
            running = true;
            worker.Run([this]() { WorkerLoop(); });
#else
            running = true;
            {
                std::lock_guard<std::mutex> lk(control_mutex_std);
                worker_thread = std::thread([this]() { WorkerLoop(); });
            }
#endif
        }

        // 停止定时器并清理任务
        void Stop() {
            {
                Mutex::Lock __(control_mutex);
                if (!running) return;
                running = false;
            }
#ifdef _WIN32
            if (stopEvent) SetEvent(stopEvent);
            if (hTimer) CancelWaitableTimer(hTimer);
            worker.Wait();
            if (hTimer) { CloseHandle(hTimer); hTimer = NULL; }
            if (stopEvent) { CloseHandle(stopEvent); stopEvent = NULL; }
#else
            {
                std::lock_guard<std::mutex> lk(control_mutex_std);
                cv_stop.notify_all();
            }
            if (worker_thread.joinable()) worker_thread.join();
#endif
            Mutex::Lock __(tasks_mutex);
            tasks.clear();
        }

        // 统一版 CreateTask：兼容 TArgs... 为空和非空两种情况
        void CreateTask(Event<R, TArgs...> cb, int cycle, TArgs... payload) {
            Mutex::Lock __(tasks_mutex);
            tasks.emplace_back(std::move(cb), cycle, std::tuple<TArgs...>(std::move(payload)...));
        }

        // 删除任务：
        // 无参数版本不支持直接删除（与旧实现保持一致）
        template <bool NoArgs = (sizeof...(TArgs) == 0), typename = std::enable_if_t<NoArgs>>
        bool RemoveTask(void*) { return false; }

        // 多参数版本：谓词以 payload tuple 作为输入，返回 true 删除该任务
        template <bool HasArgs = (sizeof...(TArgs) > 0), typename = std::enable_if_t<HasArgs>>
        bool RemoveTaskIf(const std::function<bool(const std::tuple<TArgs...>&)>& pred) {
            Mutex::Lock __(tasks_mutex);
            if (tasks.empty()) return false;
            for (auto it = tasks.begin(); it != tasks.end(); ++it) {
                if (pred(it->data)) {
                    tasks.erase(it);
                    return true;
                }
            }
            return false;
        }
        /// <summary>
		/// 查找是否存在满足条件的任务：
        /// </summary>
        /// <typeparam name=""></typeparam>
        /// <typeparam name="HasArgs"></typeparam>
        /// <param name="pred"></param>
        /// <returns></returns>
        template <bool HasArgs = (sizeof...(TArgs) > 0), typename = std::enable_if_t<HasArgs>>
        bool FindTaskIf(const std::function<bool(const std::tuple<TArgs...>&)>& pred) {
            Mutex::Lock __(tasks_mutex);
            if (tasks.empty()) return false;
            for (auto& t : tasks) {
                if (pred(t.data)) {
                    return true;
                }
            }
            return false;
		}

        bool IsRun() {
            Mutex::Lock lock(tlock);
            return running;
        }

    private:
        using TaskVector = Vector<TimerTask<R, TArgs...>>;

        TaskVector tasks;
        Mutex tasks_mutex;

        std::atomic<bool> running;
        uint64_t interval_ms;

#ifdef _WIN32
        Thread worker;
        Mutex control_mutex;
        ConditionVariable cv;
        HANDLE hTimer;
        HANDLE stopEvent;
#else
        std::thread worker_thread;
        std::mutex control_mutex_std;
        std::condition_variable cv_stop;
        Mutex control_mutex; // 与 Win32 接口统一（仅用于 IsRun 锁）
#endif

        void WorkerLoop() {
#ifdef _WIN32
            HANDLE handles[2] = { hTimer, stopEvent };
            while (running) {
                DWORD idx = WaitForMultipleObjects(2, handles, FALSE, INFINITE);
                if (idx == WAIT_OBJECT_0 + 1) break; // stopEvent
                if (idx == WAIT_OBJECT_0) { // hTimer
                    struct CallItem { SharedEvent<R, TArgs...> fn; std::tuple<TArgs...> data; };
                    Vector<CallItem> calls;
                    {
                        Mutex::Lock __(tasks_mutex);
                        for (int ti = 0; ti < tasks.size(); ++ti) {
                            auto& t = tasks[ti];
                            if (t.cycle <= 0) continue;
                            if (++t.value >= t.cycle) {
                                if (t.fn) {
                                    CallItem ci;
                                    ci.fn = t.fn;
                                    if constexpr (kPayloadCopyable)
                                        ci.data = t.data;
                                    else
                                        ci.data = std::move(t.data);
                                    calls.push_back(std::move(ci));
                                }
                                t.value = 0;
                            }
                        }
                    }
                    for (auto& c : calls) {
                        try {
                            if (!c.fn)
                                continue;
                            InvokeCall(c.fn, c.data);
                        } catch (...) {}
                    }
                }
                else {
                    break; // wait failed
                }
            }
#else
            std::unique_lock<std::mutex> lk(control_mutex_std);
            while (running) {
                auto timeout = std::chrono::milliseconds(interval_ms > 0 ? interval_ms : 1);
                cv_stop.wait_for(lk, timeout, [this]() { return !running.load(); });
                if (!running) break;

                struct CallItem { SharedEvent<R, TArgs...> fn; std::tuple<TArgs...> data; };
                Vector<CallItem> calls;
                {
                    Mutex::Lock __(tasks_mutex);
                    for (int ti = 0; ti < tasks.size(); ++ti) {
                        auto& t = tasks[ti];
                        if (t.cycle <= 0) continue;
                        if (++t.value >= t.cycle) {
                            if (t.fn) {
                                CallItem ci;
                                ci.fn = t.fn;
                                if constexpr (kPayloadCopyable)
                                    ci.data = t.data;
                                else
                                    ci.data = std::move(t.data);
                                calls.push_back(std::move(ci));
                            }
                            t.value = 0;
                        }
                    }
                }
                for (auto& c : calls) {
                    try {
                        if (!c.fn)
                            continue;
                        InvokeCall(c.fn, c.data);
                    } catch (...) {}
                }
            }
#endif
        }

        // 仅用于 IsRun 的互斥锁语义兼容
        Mutex tlock;
    };

}; // namespace AssetLib