#ifndef _NetLibrary_ThreadWork_h_
#define _NetLibrary_ThreadWork_h_
namespace AssetLib {
    template <typename U>
    class One {
    public:
        One() = default;
        One(const One&) = delete;
        One& operator=(const One&) = delete;
        One(One&& o) noexcept : ptr_(std::move(o.ptr_)) {}
        One& operator=(One&& o) noexcept { ptr_ = std::move(o.ptr_); return *this; }

        template <typename... Args>
        void Create(Args&&... args) { ptr_ = std::make_unique<U>(std::forward<Args>(args)...); }
        bool IsEmpty() const { return ptr_ == nullptr; }
        U& operator*() { return *ptr_; }
        U* operator->() { return ptr_.get(); }

    private:
        std::unique_ptr<U> ptr_;
    };

    template <typename R = void, typename... Args>
    class Event {
    public:
        Event() = default;
        Event(Event&& other) noexcept : fn_(std::move(other.fn_)) {}
        Event& operator=(Event&& other) noexcept { fn_ = std::move(other.fn_); return *this; }

        template <typename Fn>
        explicit Event(Fn&& fn) { fn_ = std::forward<Fn>(fn); }

        template <typename C>
        Event(C* obj, R(C::* method)(Args...)) {
            fn_ = [obj, method](Args... a) -> R { return (obj->*method)(std::forward<Args>(a)...); };
        }
        template <typename C>
        Event(const C* obj, R(C::* method)(Args...) const) {
            fn_ = [obj, method](Args... a) -> R { return (obj->*method)(std::forward<Args>(a)...); };
        }

        Event(const Event&) = delete;
        Event& operator=(const Event&) = delete;

        template <typename Fn>
        void operator<<(Fn&& fn) { fn_ = std::forward<Fn>(fn); }

        // 新增：支持从 Event 移动绑定内部 callable，避免把 Event 作为 std::function 目标
        void operator<<(Event&& other) { fn_ = std::move(other.fn_); }
        void MoveFrom(Event&& other) { fn_ = std::move(other.fn_); }

        template <typename Fn>
        Event& operator=(Fn&& fn) { fn_ = std::forward<Fn>(fn); return *this; }

        explicit operator bool() const { return static_cast<bool>(fn_); }

        R operator()(Args... args) {
            if (fn_) return fn_(std::forward<Args>(args)...);
            if constexpr (!std::is_void_v<R>) {
                return R{};
            }
        }

    private:
        std::function<R(Args...)> fn_;
    };

    template <typename T>
    class BiVector {
    public:
        // 避免拷贝：仅提供移动/原地构造
        void CreateHead(T&& v) { dq_.push_front(std::move(v)); }
        void CreateTail(T&& v) { dq_.push_back(std::move(v)); }
        template <typename... Args>
        void CreateHead(Args&&... args) { dq_.emplace_front(std::forward<Args>(args)...); }
        template <typename... Args>
        void CreateTail(Args&&... args) { dq_.emplace_back(std::forward<Args>(args)...); }

        bool IsEmpty() const { return dq_.empty(); }
        int GetCount() const { return static_cast<int>(dq_.size()); }

        T& Head() { return dq_.front(); }
        T& Tail() { return dq_.back(); }
        void DropHead() { dq_.pop_front(); }
        void DropTail() { dq_.pop_back(); }
        T PopTail() { T t = std::move(dq_.back()); dq_.pop_back(); return t; }
    private:
        std::deque<T> dq_;
    };

    class Mutex {
    public:
        using Lock = std::unique_lock<Mutex>;
        friend class ConditionVariable;

        void lock() { m_.lock(); }
        void unlock() { m_.unlock(); }

    private:
        std::mutex m_;
    };

    class ConditionVariable {
    public:
        template <typename Pred>
        void Wait(Mutex& m, Pred pred) {
            std::unique_lock<std::mutex> lk(m.m_);
            cv_.wait(lk, pred);
        }
        void Signal() { cv_.notify_one(); }
        void Broadcast() { cv_.notify_all(); }
    private:
        std::condition_variable cv_;
    };

    class Thread {
    public:
        Thread() = default;
        ~Thread() { if (th_.joinable()) th_.join(); }

        template <typename Fn>
        void Run(Fn&& fn) {
            if (th_.joinable()) th_.join();
            th_ = std::thread(std::forward<Fn>(fn));
        }
        void Wait() { if (th_.joinable()) th_.join(); }
        int GetId() const { return th_.get_id() == std::thread::id() ? -1 : 1; }

    private:
        std::thread th_;
    };

    template <typename T>
    using Vector = std::vector<T>;

#ifndef LOG
#define LOG(x) ((void)0)
#endif

    inline int CPU_Cores() {
        unsigned int c = std::thread::hardware_concurrency();
        return c == 0 ? 4 : static_cast<int>(c);
    }
    template <typename T>
    using Moveable = T;

    template<typename T>
    struct IsOne : std::false_type {};
    template<typename U>
    struct IsOne<One<U>> : std::true_type {};
    template<typename T>
    constexpr bool IsOneV = IsOne<T>::value;

    template<typename T>
    inline bool IsEmptyValue(const T& value) {
        if constexpr (IsOneV<T>) {
            return value.IsEmpty();
        }
        else if constexpr (std::is_pointer_v<T>) {
            return value == nullptr;
        }
        else if constexpr (std::is_same_v<T, std::nullptr_t>) {
            return true;
        }
        else {
            return false;
        }
    }

    template <typename R = void, typename... T>
    struct TreadTask {
        std::tuple<T...> obj;
        // 关键：事件参数改为引用，避免对不可拷贝类型的按值传参
        Event<R, T&...> ev;

        using ReturnT = std::conditional_t<std::is_void_v<R>, void, R>;
        ReturnT execute() {
            if constexpr (std::is_void_v<R>) {
                std::apply(ev, obj);
            }
            else {
                return std::apply(ev, obj);
            }
        }
        TreadTask(Event<R, T&...> cb, std::tuple<T...> t) : obj(std::move(t)) { ev << std::move(cb); }
        std::tuple<T...>& GetObjectTuple() { return obj; }
    };

    template <typename R = void, typename... T>
    class ThreadWork : public Thread {
    private:
        std::atomic<bool> is_maintask = false;
        std::atomic<bool> is_running = false;
        std::atomic<bool> should_exit = false;
        One<std::tuple<T...>> main_obj;

        BiVector<TreadTask<R, T...>> task_queue;
        Mutex queue_lock;
        ConditionVariable task_condition;

        // 关键：主任务事件也改为引用形参版本
        Event<R, T&...> TaskEv;

        void RunThread()
        {
            while (!should_exit) {
                bool had_task = false;
                One<TreadTask<R, T...>> pending_task;
                bool run_main = false;

                // 非主任务路径：等待直到有任务或需要退出
                if (!is_maintask) {
                    task_condition.Wait(queue_lock, [this] {
                        return should_exit || !task_queue.IsEmpty();
                    });
                    if (should_exit && task_queue.IsEmpty())
                        break;
                    {
                        Mutex::Lock __(queue_lock);
                        if (!task_queue.IsEmpty()) {
                            auto tmp = task_queue.PopTail();
                            pending_task.Create(std::move(tmp));
                            had_task = true;
                        }
                    }
                }
                else {
                    // 主任务路径：仅在有 TaskEv 且非退出时执行一次
                    {
                        Mutex::Lock __(queue_lock);
                        run_main = (bool)TaskEv && !should_exit;
                    }
                }

                if (had_task) {
                    is_running = true;
                    pending_task->execute();
                    is_running = false;
                    continue;
                }

                if (run_main) {
                    is_running = true;
                    std::apply(TaskEv, *main_obj);
                    is_running = false;

                    // 等待外部唤醒后退出
                    task_condition.Wait(queue_lock, [this] {
                        return should_exit.load();
                    });
                    should_exit = true;
                }
            }
        }

    public:
        ThreadWork() : should_exit(false), is_running(false) {}
        ~ThreadWork() { Stop(); }

        ThreadWork(const ThreadWork&) = delete;
        ThreadWork& operator=(const ThreadWork&) = delete;
        ThreadWork(ThreadWork&&) = delete;
        ThreadWork& operator=(ThreadWork&&) = delete;

        template <typename... U>
        void Set(Event<R, T&...>&& cb, U&&... t)
        {
            static_assert(sizeof...(U) == sizeof...(T), "ThreadWork::Set requires one argument per callback parameter");
            main_obj.Create(std::tuple<T...>(std::forward<U>(t)...));
            TaskEv.MoveFrom(std::move(cb));
            is_maintask = true;
        }

        void SubmitTask(Event<R, T&...> task, std::tuple<T...> t, bool isSignal = true, bool isHead = true) {
            {
                Mutex::Lock __(queue_lock);
                if (isHead)
                    task_queue.CreateHead(std::move(task), std::move(t));
                else
                    task_queue.CreateTail(std::move(task), std::move(t));
            }
            if (isSignal)
                task_condition.Signal();
        }

        void SubmitTask(Event<R, T&...> task, T... args, bool isSignal = true, bool isHead = true) {
            std::tuple<T...> t(std::forward<T>(args)...);
            SubmitTask(std::move(task), std::move(t), isSignal, isHead);
        }

        int GetPendingTaskCount()
        {
            Mutex::Lock __(queue_lock);
            return task_queue.GetCount();
        }

        void ThreadRun()
        {
            // C++20 明确捕获 this，避免隐式捕获警告
            this->Run([this] { RunThread(); });
        }

        void Stop()
        {
            should_exit = true;
            task_condition.Broadcast();
            this->Wait();
        }
        bool GetIsLoop() {
            Mutex::Lock __(queue_lock);
            return should_exit.load();
        }
        bool GetIsRun() {
            Mutex::Lock __(queue_lock);
            return is_running;
        }
        int Getid() { return this->GetId(); }
    };

    template <typename R = void, typename... T>
    class ThreadWorkPool {
    private:
        Vector<std::unique_ptr<ThreadWork<R, T...>>> workers;
        int thread_count = 0;
        int next_thread = 0;
        Mutex pool_lock;
        // 关键：池内主任务事件同样改为引用形参版本
        Event<R, T&...> WorkerMainTask;

        struct {
            size_t total_tasks_submitted = 0;
            size_t total_tasks_completed = 0;
            time_t last_stats_time = 0;
        } stats;

    private:
        ThreadWork<R, T...>* GetNextWorker()
        {
            Mutex::Lock __(pool_lock);
            if (workers.empty())
                return nullptr;

            ThreadWork<R, T...>* worker = workers[next_thread].get();
            next_thread = (next_thread + 1) % static_cast<int>(workers.size());

            int min_tasks = worker->GetPendingTaskCount();
            for (int i = 1; i < static_cast<int>(workers.size()); i++) {
                int idx = (next_thread + i) % static_cast<int>(workers.size());
                ThreadWork<R, T...>* candidate = workers[idx].get();
                int candidate_tasks = candidate->GetPendingTaskCount();
                if (candidate_tasks < min_tasks) {
                    worker = candidate;
                    min_tasks = candidate_tasks;
                }
            }
            return worker;
        }

        ThreadWork<R, T...>* GetWorkerByFd(int fd)
        {
            Mutex::Lock __(pool_lock);
            if (workers.empty())
                return nullptr;
            int idx = std::abs(fd) % static_cast<int>(workers.size());
            return workers[idx].get();
        }

    public:
        ThreadWorkPool()
        {
            std::memset(&stats, 0, sizeof(stats));
            stats.last_stats_time = std::time(nullptr);
        }

        ~ThreadWorkPool() { StopAll(); }

        static int CalculateOptimalThreadCount()
        {
            int cpu_cores = CPU_Cores();
            int optimal = std::min(std::max(cpu_cores * 2, 4), 32);
            LOG("CPU cores: " << cpu_cores << ", Optimal thread count: " << optimal);
            return optimal;
        }

        bool Create(int count = 0, std::tuple<T...> t = std::tuple<T...>())
        {
            Mutex::Lock __(pool_lock);
            thread_count = count <= 0 ? CalculateOptimalThreadCount() : count;
            workers.clear();
            workers.reserve(thread_count);
            for (int i = 0; i < thread_count; ++i) {
                workers.emplace_back(std::make_unique<ThreadWork<R, T...>>());
                if (WorkerMainTask) {
                    std::apply(
                        [&](auto&&... args) {
                        workers.back()->Set(std::move(WorkerMainTask), std::move(args)...);
                    },
                        t
                    );
                }
            }
            LOG("Thread pool created with " << thread_count << " workers");
            return true;
        }

        bool Create(int count, Event<R, T&...> task, std::tuple<T...> t)
        {
            WorkerMainTask << std::move(task);
            return Create(count, std::move(t));
        }

        bool Create(int count, Event<R, T&...> task, T... args) {
            WorkerMainTask << std::move(task);
            return Create(count, std::tuple<T...>(std::forward<T>(args)...));
        }

        void StartAll()
        {
            Mutex::Lock __(pool_lock);
            for (auto& workerPtr : workers) {
                auto& worker = *workerPtr;
                if (!worker.GetIsLoop()) {
                    worker.ThreadRun();
                }
            }
            LOG("All worker threads started");
        }

        void StopAll()
        {
            Mutex::Lock __(pool_lock);
            if (workers.empty())
                return;
            for (auto& workerPtr : workers)
                workerPtr->Stop();
            workers.clear();
            LOG("All worker threads stopped");
        }

        void SubmitTask(Event<R, T&...> task, T... args, int fd = -1, bool isSignal = true, bool isHead = true) {
            if (WorkerMainTask)
                return;
            ThreadWork<R, T...>* worker = nullptr;
            if (fd >= 0)
                worker = GetWorkerByFd(fd);
            else
                worker = GetNextWorker();

            if (worker) {
                worker->SubmitTask(std::move(task), std::forward<T>(args)..., isSignal, isHead);
                stats.total_tasks_submitted++;
            }
            LogStats();
        }

        int GetThreadCount() const { return thread_count; }
        int GetActiveThreadCount()
        {
            Mutex::Lock __(pool_lock);
            int active_count = 0;
            for (auto& workerPtr : workers) {
                if (workerPtr->GetIsRun())
                    active_count++;
            }
            return active_count;
        }
        void GetPoolStatus(Vector<int>& pending_tasks)
        {
            Mutex::Lock __(pool_lock);
            pending_tasks.clear();
            for (auto& workerPtr : workers) {
                pending_tasks.push_back(workerPtr->GetPendingTaskCount());
            }
        }

    private:
        void LogStats()
        {
            time_t now = std::time(nullptr);
            if (now - stats.last_stats_time >= 30) {
                double tasks_per_sec =
                    (stats.total_tasks_submitted - stats.total_tasks_completed) / 30.0;
                LOG("ThreadPool stats - Tasks submitted: "
                    << stats.total_tasks_submitted << ", Completed: " << stats.total_tasks_completed
                    << ", Pending: " << (stats.total_tasks_submitted - stats.total_tasks_completed)
                    << ", Rate: " << tasks_per_sec << " tasks/s");
                stats.last_stats_time = now;
            }
        }
    };
};
#endif //_NetLibrary_ThreadWork_h_