#ifndef _NetLibrary_EventEmitter_h_
#define _NetLibrary_EventEmitter_h_
template <typename E,typename V>
class EventEmitter {
	private:
    struct Listener : Moveable<Listener> {
        Function<void (V)> callback;
        bool once;
        Listener(Function<void (V)> cb, bool isOnce)
        {
            callback << cb;
            once = isOnce;
        }
    };

    VectorMap<E, Vector<Listener>> events;
    Mutex mutex;

public:
    // 绑定事件
    void On(const E& event, Function<void (V)> callback) {
        Mutex::Lock lock(mutex);
        Vector<String> ss;
        events.GetAdd(event).Add({callback, false});
    }

    // 绑定一次性事件
    void Once(const E& event, Function<void (V)> callback) {
        Mutex::Lock lock(mutex);
        events.GetAdd(event).Add({callback, true});
    }

    // 触发事件
    void Emit(const E& event, V&& data = V()) {
        Mutex::Lock lock(mutex);
        int index = events.Find(event);
        if (index < 0) return;

        Vector<Listener>& listeners = events[index];
        Vector<Listener> remaining;

        for (auto& listener : listeners) {
            listener.callback(data);
            if (!listener.once) {
                remaining.Add(listener);
            }
        }

        if (remaining.IsEmpty())
            events.Unlink(index);
        else
            listeners = pick(remaining);
    }
    template <typename... Args>
    void Emit(const E& event, const char* s, const Args&... args) {
        Mutex::Lock lock(mutex);
        // 构建消息内容
        String content;
        try {
            if constexpr (sizeof...(args) > 0) {
                content = Format(s, gather<Vector<Value>>(args...));
            }
            else {
                content = s;
            }
        }
        catch (...) {
            content = "Format error in message: " + String(s);
        }
		Emit(event, V(~content));
	}
    // 移除事件监听器
    void Off(const E& event, Function<void (V)> callback = nullptr) {
        Mutex::Lock lock(mutex);
        int index = events.Find(event);
        if (index < 0) return;

        if (!callback) {
            // 移除该事件的所有监听器
            events.Unlink(index);
            return;
        }

        // 移除特定的监听器
        Vector<Listener>& listeners = events[index];
        Vector<Listener> remaining;

        for (auto& listener : listeners) {
            if (listener.callback != callback) {
                remaining.Add(listener);
            }
        }

        if (remaining.IsEmpty())
            events.Unlink(index);
        else
            listeners = pick(remaining);
    }

    // 移除所有事件监听器
    void RemoveAllListeners() {
        Mutex::Lock lock(mutex);
        events.Clear();
    }
};
#endif
