#pragma once

namespace AssetLib {

    // 事件处理器统一签名：接受 any 向量作为参数
    using EventArgs = std::vector<std::any>;
    using EventHandler = std::function<void(const EventArgs&)>;
    using EventHandlerR = std::function<std::any(const EventArgs&)>;

    // 订阅返回的标记，用于取消订阅
    template <typename TKey>
    struct SubscriptionToken
    {
        TKey event{};
        uint64_t id{ 0 };

        bool valid() const noexcept { return id != 0; }
    };

    template <typename TKey = std::string,
        typename THash = std::hash<TKey>,
        typename TEqual = std::equal_to<TKey>>
        class EventBus
    {
    public:
        EventBus() = default;
        ~EventBus() { ClearAll(); };

        // 注册事件处理器，返回可用于取消订阅的 token
        SubscriptionToken<TKey> On(const TKey& event, EventHandler handler)
        {
            if (!handler) return {};
            const uint64_t id = nextId();
            {
                std::unique_lock<std::shared_mutex> lock(_mutex);
                _handlers[event].emplace_back(HandlerEntry{ id, std::move(handler), false });
            }
			
#ifdef _DEBUG
            fprintf(stderr, "[EventBus] On this=%p key=%s count=%zu\n", this, event.c_str(), _handlers[event].size());
#endif
            return SubscriptionToken<TKey>{ event, id };
        }

        // 注册带返回值的处理器
        SubscriptionToken<TKey> OnR(const TKey& event, EventHandlerR handler)
        {
            if (!handler) return {};
            const uint64_t id = nextId();
            {
                std::unique_lock<std::shared_mutex> lock(_mutex);
                _handlers_r[event].emplace_back(HandlerEntryR{ id, std::move(handler), false });
            }
            return SubscriptionToken<TKey>{ event, id };
        }

        // 注册一次性处理器，触发后自动移除
        SubscriptionToken<TKey> Once(const TKey& event, EventHandler handler)
        {
            if (!handler) return {};
            const uint64_t id = nextId();
            {
                std::unique_lock<std::shared_mutex> lock(_mutex);
                _handlers[event].emplace_back(HandlerEntry{ id, std::move(handler), true });
            }
            return SubscriptionToken<TKey>{ event, id };
        }

        // 注册一次性带返回值的处理器
        SubscriptionToken<TKey> OnceR(const TKey& event, EventHandlerR handler)
        {
            if (!handler) return {};
            const uint64_t id = nextId();
            {
                std::unique_lock<std::shared_mutex> lock(_mutex);
                _handlers_r[event].emplace_back(HandlerEntryR{ id, std::move(handler), true });
            }
            return SubscriptionToken<TKey>{ event, id };
        }

        // 绑定对象成员方法（原始指针版本，调用者负责对象有效期）
        template <typename T>
        SubscriptionToken<TKey> On(const TKey& event, T* obj, void (T::* method)(const EventArgs&))
        {
            if (obj == nullptr || method == nullptr) return {};
            return On(event, [obj, method](const EventArgs& args) {
                (obj->*method)(args);
            });
        }
        // 绑定对象成员方法（原始指针版本，调用者负责对象有效期）
        template <typename T>
        SubscriptionToken<TKey> OnR(const TKey& event, T* obj, std::any (T::* method)(const EventArgs&))
        {
            if (obj == nullptr || method == nullptr) return {};
            return OnR(event, [obj, method](const EventArgs& args) {
                return (obj->*method)(args);
            });
		}
        template <typename T>
        SubscriptionToken<TKey> Once(const TKey& event, T* obj, void (T::* method)(const EventArgs&))
        {
            if (obj == nullptr || method == nullptr) return {};
            return Once(event, [obj, method](const EventArgs& args) {
                (obj->*method)(args);
            });
        }

        // 绑定对象成员方法（shared_ptr 版本，内部使用 weak_ptr 防止悬挂）
        template <typename T>
        SubscriptionToken<TKey> On(const TKey& event, const std::shared_ptr<T>& obj, void (T::* method)(const EventArgs&))
        {
            if (!obj || method == nullptr) return {};
            std::weak_ptr<T> weak = obj;
            return On(event, [weak, method](const EventArgs& args) {
                if (auto locked = weak.lock())
                {
                    (locked.get()->*method)(args);
                }
            });
        }

        template <typename T>
        SubscriptionToken<TKey> Once(const TKey& event, const std::shared_ptr<T>& obj, void (T::* method)(const EventArgs&))
        {
            if (!obj || method == nullptr) return {};
            std::weak_ptr<T> weak = obj;
            return Once(event, [weak, method](const EventArgs& args) {
                if (auto locked = weak.lock())
                {
                    (locked.get()->*method)(args);
                }
            });
        }

        // 取消订阅（通过 token）
        bool Off(const SubscriptionToken<TKey>& token)
        {
            if (!token.valid()) return false;
            std::unique_lock<std::shared_mutex> lock(_mutex);
            auto it = _handlers.find(token.event);
            if (it == _handlers.end()) return false;
            auto& vec = it->second;
            for (auto vit = vec.begin(); vit != vec.end(); ++vit)
            {
                if (vit->id == token.id)
                {
                    vec.erase(vit);
                    if (vec.empty())
                        _handlers.erase(it);
                    return true;
                }
            }
            return false;
        }

        // 取消某个事件的所有订阅
        bool OffAll(const TKey& event)
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);
            return _handlers.erase(event) > 0;
        }

        // 清除所有事件订阅（含有返回值与无返回值两类）
        void ClearAll()
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);
            _handlers.clear();
            _handlers_r.clear();
        }

        // 触发事件（任意参数），线程安全且避免长时间持锁
        template <typename... Args>
        void Emit(const TKey& event, Args&&... args)
        {
            EventArgs packed;
            packed.reserve(sizeof...(Args));
            (packArg(packed, std::forward<Args>(args)), ...);

            // 复制快照，降低锁持有时间，避免处理器执行期间锁住注册表
            std::vector<HandlerEntry> snapshot;
            {
                std::shared_lock<std::shared_mutex> lock(_mutex);
                auto it = _handlers.find(event);
                if (it != _handlers.end())
                    snapshot = it->second;
            }

            // 收集一次性订阅的 id，避免在回调期间修改订阅表
            std::vector<uint64_t> toRemove;
            toRemove.reserve(snapshot.size());

            // 无锁执行处理器，防止用户回调阻塞总线
            for (const auto& h : snapshot)
            {
                if (h.once)
                    toRemove.emplace_back(h.id);
                h.fn(packed);
            }
#ifdef _DEBUG
            fprintf(stderr, "[EventBus] Emit this=%p key=%s\n", this, event.c_str());
#endif
            // 批量移除一次性订阅（单次写锁，避免多次加锁）
            if (!toRemove.empty())
                removeByIds(event, toRemove);
        }

        // 触发事件（已打包的参数）
        void Emit(const TKey& event, const EventArgs& args)
        {
            std::vector<HandlerEntry> snapshot;
            {
                std::shared_lock<std::shared_mutex> lock(_mutex);
                auto it = _handlers.find(event);
                if (it != _handlers.end())
                    snapshot = it->second;
            }

            std::vector<uint64_t> toRemove;
            toRemove.reserve(snapshot.size());

            for (const auto& h : snapshot)
            {
                if (h.once)
                    toRemove.emplace_back(h.id);
                h.fn(args);
            }
#ifdef _DEBUG
            fprintf(stderr, "[EventBus] Emit this=%p key=%s\n", this, event.c_str());
#endif
            if (!toRemove.empty())
                removeByIds(event, toRemove);
        }

        // 触发事件并收集返回值
        template <typename... Args>
        std::vector<std::any> EmitR(const TKey& event, Args&&... args)
        {
            EventArgs packed;
            packed.reserve(sizeof...(Args));
            (packArg(packed, std::forward<Args>(args)), ...);

            std::vector<HandlerEntryR> snapshot;
            {
                std::shared_lock<std::shared_mutex> lock(_mutex);
                auto it = _handlers_r.find(event);
                if (it != _handlers_r.end())
                    snapshot = it->second;
            }

            std::vector<uint64_t> toRemove;
            toRemove.reserve(snapshot.size());

            std::vector<std::any> results;
            results.reserve(snapshot.size());
            for (const auto& h : snapshot)
            {
                if (h.once)
                    toRemove.emplace_back(h.id);
                results.emplace_back(h.fn(packed));
            }

            if (!toRemove.empty())
                removeByIdsR(event, toRemove);

            return results;
        }

        // 触发事件并收集返回值（已打包的参数）
        std::vector<std::any> EmitR(const TKey& event, const EventArgs& args)
        {
            std::vector<HandlerEntryR> snapshot;
            {
                std::shared_lock<std::shared_mutex> lock(_mutex);
                auto it = _handlers_r.find(event);
                if (it != _handlers_r.end())
                    snapshot = it->second;
            }

            std::vector<uint64_t> toRemove;
            toRemove.reserve(snapshot.size());

            std::vector<std::any> results;
            results.reserve(snapshot.size());
            for (const auto& h : snapshot)
            {
                if (h.once)
                    toRemove.emplace_back(h.id);
                results.emplace_back(h.fn(args));
            }

            if (!toRemove.empty())
                removeByIdsR(event, toRemove);

            return results;
        }

        // 助手：安全获取参数（类型检查）
        template <typename T>
        static const T* ArgAs(const EventArgs& args, size_t index)
        {
            if (index >= args.size()) return nullptr;
            const std::any& a = args[index];
            return std::any_cast<T>(&a);
        }

    private:
        struct HandlerEntry
        {
            uint64_t id;
            EventHandler fn;
            bool once;
        };

        struct HandlerEntryR
        {
            uint64_t id;
            EventHandlerR fn;
            bool once;
        };

        static void packArg(EventArgs& dst, const std::any& a) { dst.emplace_back(a); }
        template <typename T>
        static void packArg(EventArgs& dst, T&& v) { dst.emplace_back(std::forward<T>(v)); }

        // 新增：指针字符串转为 std::string 深拷贝
        static void packArg(EventArgs& dst, const char* s) {
            dst.emplace_back(std::string{s ? s : ""});
        }
        static void packArg(EventArgs& dst, char* s) {
            dst.emplace_back(std::string{s ? s : ""});
        }

        static uint64_t nextId()
        {
            static std::atomic<uint64_t> s_id{ 1 };
            return s_id.fetch_add(1, std::memory_order_relaxed);
        }

        void removeByIds(const TKey& event, const std::vector<uint64_t>& ids)
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);
            auto it = _handlers.find(event);
            if (it == _handlers.end()) return;
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [&](const HandlerEntry& e)
            {
                return std::find(ids.begin(), ids.end(), e.id) != ids.end();
            }),
                vec.end());
            if (vec.empty())
                _handlers.erase(it);
        }

        void removeByIdsR(const TKey& event, const std::vector<uint64_t>& ids)
        {
            std::unique_lock<std::shared_mutex> lock(_mutex);
            auto it = _handlers_r.find(event);
            if (it == _handlers_r.end()) return;
            auto& vec = it->second;
            vec.erase(std::remove_if(vec.begin(), vec.end(),
                [&](const HandlerEntryR& e)
            {
                return std::find(ids.begin(), ids.end(), e.id) != ids.end();
            }),
                vec.end());
            if (vec.empty())
                _handlers_r.erase(it);
        }

        std::unordered_map<TKey, std::vector<HandlerEntry>, THash, TEqual> _handlers;
        std::unordered_map<TKey, std::vector<HandlerEntryR>, THash, TEqual> _handlers_r;
        mutable std::shared_mutex _mutex;
    };

}; // namespace NetLib