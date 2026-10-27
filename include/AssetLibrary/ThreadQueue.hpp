#pragma once

namespace AssetLib {

template <class T, typename Ret>
class QueueBase {
public:
    virtual ~QueueBase() = default;
    virtual T* GetPtr() = 0;
    virtual void ExecuteEv() = 0;
};

template <class T, typename Ret, class U = T>
class QueueNode : public QueueBase<T, Ret> {
    static_assert(std::is_base_of_v<T, U>, "U must derive from T");
private:
    std::function<Ret(T*)> ev;
    U obj;

public:
    template <typename... Args, typename = std::enable_if_t<std::is_constructible_v<U, Args&&...>>>
    QueueNode(Args&&... args)
        : obj(std::forward<Args>(args)...) {}

    T* GetPtr() override { return static_cast<T*>(&obj); }

    void SetEvent(std::function<Ret(T*)> cb) { ev = std::move(cb); }

    void ExecuteEv() override { if (ev) (void)ev(GetPtr()); }
};

template <class T, typename Ret>
class ThreadQueue {
private:
    using QueuePtr = std::unique_ptr<QueueBase<T, Ret>>;

    template <class Fn, class U>
    static constexpr bool IsQueueCallbackV =
        std::is_void_v<Ret>
            ? (std::is_invocable_v<Fn&, U*> || std::is_invocable_v<Fn&, T*>)
            : (std::is_invocable_r_v<Ret, Fn&, U*> || std::is_invocable_r_v<Ret, Fn&, T*>);

    std::atomic<bool> isclosed{false};
    std::thread worker_;
    std::mutex qLock_;
    std::condition_variable cv_;
    std::deque<QueuePtr> inQueue_;
    std::deque<QueuePtr> outQueue_;

    void Execute() {
        while (!isclosed.load(std::memory_order_acquire)) {
            {
                std::unique_lock<std::mutex> lock(qLock_);
                // 等待有任务或关闭
                cv_.wait(lock, [this] {
                    return isclosed.load(std::memory_order_acquire)
                           || !inQueue_.empty()
                           || !outQueue_.empty();
                });
                if (isclosed.load(std::memory_order_acquire))
                    break;
                // inQueue_ 转移到 outQueue_
                while (!inQueue_.empty()) {
                    outQueue_.push_back(std::move(inQueue_.front()));
                    inQueue_.pop_front();
                }
            }
            // 执行 outQueue_ 任务（串行）
            for (;;) {
                QueuePtr obj;
                {
                    std::lock_guard<std::mutex> lock(qLock_);
                    if (outQueue_.empty())
                        break;
                    obj = std::move(outQueue_.front());
                    outQueue_.pop_front();
                }
                if (obj)
                    obj->ExecuteEv();
            }
        }
    }

public:
    ThreadQueue() { Start(); }
    ~ThreadQueue() { Stop(); }

    void Start() {
        isclosed.store(false, std::memory_order_release);
        if (worker_.joinable())
            return;
        worker_ = std::thread([this] { Execute(); });
    }

    void Stop() {
        isclosed.store(true, std::memory_order_release);
        cv_.notify_all();
        if (worker_.joinable())
            worker_.join();
        std::lock_guard<std::mutex> lock(qLock_);
        inQueue_.clear();
        outQueue_.clear();
    }

    // 保留原有方法名（CreatQueue）以避免调用方改动
    template <class U = T, class Fn, class... Args,
              class = std::enable_if_t<
                  std::is_base_of_v<T, U>
                  && std::is_constructible_v<U, Args&&...>
                  && !std::is_constructible_v<QueuePtr, Fn&&>
                  && IsQueueCallbackV<Fn, U>>>
    void CreatQueue(Fn&& cb, bool isStart, Args&&... args) {
        std::function<Ret(T*)> basecb = [f = std::forward<Fn>(cb)](T* p) mutable -> Ret {
            if constexpr (std::is_void_v<Ret>) {
                if constexpr (std::is_invocable_v<Fn&, U*>)
                    f(static_cast<U*>(p));
                else
                    f(p);
            }
            else {
                if constexpr (std::is_invocable_r_v<Ret, Fn&, U*>)
                    return f(static_cast<U*>(p));
                else
                    return f(p);
            }
        };

        auto q = std::make_unique<QueueNode<T, Ret, U>>(std::forward<Args>(args)...);
        q->SetEvent(std::move(basecb));
        {
            std::lock_guard<std::mutex> lock(qLock_);
            inQueue_.push_back(std::move(q));
        }
        if (isStart)
            cv_.notify_one();
    }

    // 新增：基于 unique_ptr 的添加接口，替代 U++ 的 One<> 和 pick()
    void AddQueue(std::unique_ptr<QueueBase<T, Ret>> q, bool isStart) {
        {
            std::lock_guard<std::mutex> lock(qLock_);
            inQueue_.push_back(std::move(q));
        }
        if (isStart)
            cv_.notify_one();
    }

    void SignalQueue() {
        cv_.notify_one();
    }
};

} // namespace NetLibrary
