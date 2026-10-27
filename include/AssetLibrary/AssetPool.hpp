#pragma once

namespace AssetLib {

    struct PoolParent
    {
        using PClass = PoolParent;
        size_t linkid = static_cast<size_t>(-1);
        size_t key = static_cast<size_t>(-1);
        int64_t utc = -1;

        template <typename T>
        T* GetClass() { return dynamic_cast<T*>(this); }

        PoolParent() = default;
        virtual ~PoolParent() {
            linkid = key = static_cast<size_t>(-1);
            utc = -1;
        }
        virtual void Release() = 0;
    };

    template <typename T, class K>
    class PoolLib
    {
        static_assert(std::is_base_of_v<PoolParent, T>,
            "Template parameter T must inherit from PoolParent");
        using pob = typename T::PClass;

    private:
        // 主容器：key -> unique_ptr 对象
        std::unordered_map<K, std::unique_ptr<pob>> c_;
        // 空闲/忙碌集合：保存原始指针，用于快速分组
        std::unordered_set<pob*> idle_;
        std::unordered_set<pob*> busy_;
        size_t autoincrement_ = 0;
        std::mutex plock_;

        struct HotConfig {
            int     min_idle = 0;      // 最少空闲对象
            int     max_total = 0;      // 最大总对象（0=不限制）
            int     refill_batch = 0;      // 批量补充
            int     shrink_batch = 0;      // 批量收缩
            int64_t idle_timeout = 0;      // 空闲超时秒（0=不销毁）
            double  high_busy_ratio = 0.0;    // 高忙比阈值
            double  low_busy_ratio = 0.0;    // 低忙比阈值
        } hotcfg_;

        static int64_t NowSeconds() {
            using namespace std::chrono;
            return duration_cast<seconds>(system_clock::now().time_since_epoch()).count();
        }

        T* CreateNewOneUnlocked(bool is_busy) {
            autoincrement_++;
            // 注意：要求 T 有接受 size_t 的构造函数，如原版代码
            auto nt = std::make_unique<T>(autoincrement_);
            nt->key = autoincrement_;
            nt->utc = NowSeconds();

            T* raw = nt->template GetClass<T>();
            c_.emplace(autoincrement_, std::move(nt));

            if (is_busy)
                busy_.insert(raw);
            else
                idle_.insert(raw);
            return raw;
        }

    public:
        PoolLib() = default;
        ~PoolLib() { Clear(); }

        void Clear() {
            std::lock_guard<std::mutex> _(plock_);
            c_.clear();
            idle_.clear();
            busy_.clear();
            autoincrement_ = 0;
        }

        T* FindKey(const K& k) {
            std::lock_guard<std::mutex> _(plock_);
            auto it = c_.find(k);
            return (it != c_.end()) ? it->second->template GetClass<T>() : nullptr;
        }

        T* CreatNewOne(bool is_busy = false) {
            std::lock_guard<std::mutex> _(plock_);
            return CreateNewOneUnlocked(is_busy);
        }

        void RefillIdle(int count) {
            if (count <= 0) return;
            std::lock_guard<std::mutex> _(plock_);
            for (int i = 0; i < count; ++i)
                CreateNewOneUnlocked(false);
        }

        void EnsureMinIdle(int min_idle, int batch = 0) {
            if (min_idle <= 0) return;
            std::lock_guard<std::mutex> _(plock_);
            int need = min_idle - static_cast<int>(idle_.size());
            if (need <= 0) return;
            int to_create = need;
            if (batch > 0 && batch > need)
                to_create = batch;
            for (int i = 0; i < to_create; ++i)
                CreateNewOneUnlocked(false);
        }

        void ShrinkIdleByCount(int count) {
            if (count <= 0) return;
            std::lock_guard<std::mutex> _(plock_);
            for (int k = 0; k < count && !idle_.empty(); ++k) {
                pob* oldest = nullptr;
                int64_t oldest_utc = 0;
                bool has = false;
                for (auto* a : idle_) {
                    if (!has || a->utc < oldest_utc) { oldest = a; oldest_utc = a->utc; has = true; }
                }
                if (!oldest) break;
                idle_.erase(oldest);
                oldest->Release();
                // 从主容器删除
                auto it = c_.find(static_cast<K>(oldest->key));
                if (it != c_.end())
                    c_.erase(it);
            }
        }

        void CreatNewList(int Num) {
            std::lock_guard<std::mutex> _(plock_);
            for (int i = 0; i < Num; ++i)
                CreateNewOneUnlocked(false);
        }

        void ReleaseOb(pob* o) {
            BackTo(o);
        }

        T* TakeOut(bool isadd = false) {
            std::lock_guard<std::mutex> _(plock_);
            if (!idle_.empty()) {
                // 取出任意一个空闲对象（类似原版 Pop 行为）
                auto it = idle_.begin();
                pob* p = *it;
                idle_.erase(it);
                p->utc = NowSeconds();
                T* t = p->template GetClass<T>();
                busy_.insert(p);
                return t;
            }
            if (isadd)
                return CreateNewOneUnlocked(true);
            return nullptr;
        }

        struct PoolStats {
            int idle_count;
            int busy_count;
            int total_count;
            size_t next_key;
        };

        PoolStats GetStats() {
            std::lock_guard<std::mutex> _(plock_);
            return {
                static_cast<int>(idle_.size()),
                static_cast<int>(busy_.size()),
                static_cast<int>(c_.size()),
                autoincrement_ + 1
            };
        }

        // 注意：与原版接口保持相似，返回引用以便遍历
        std::unordered_map<K, std::unique_ptr<pob>>& GetListOb() { return c_; }
        const std::unordered_map<K, std::unique_ptr<pob>>& GetListOb() const { return c_; }

        const std::unordered_set<pob*>& GetIdle() { return idle_; }
        const std::unordered_set<pob*>& GetIdle() const { return idle_; }

        int GetIdleNum() {
            std::lock_guard<std::mutex> _(plock_);
            return static_cast<int>(idle_.size());
        }

        int GetBusyNum() {
            std::lock_guard<std::mutex> _(plock_);
            return static_cast<int>(busy_.size());
        }

        int GetAllNum() {
            std::lock_guard<std::mutex> _(plock_);
            return static_cast<int>(c_.size());
        }

        T* FindLinkId(int id) {
            std::lock_guard<std::mutex> _(plock_);
            T* tp = nullptr;
            for (auto& kv : c_) {
                auto* a = kv.second.get();
                if (a->linkid == static_cast<size_t>(id))
                    tp = a->template GetClass<T>();
            }
            return tp;
        }

        void ReleaseIdle(int64_t timeout) {
            if (timeout <= 0) return;
            int64_t now = NowSeconds();
            std::lock_guard<std::mutex> _(plock_);
            std::vector<pob*> to_remove;
            for (auto* a : idle_) {
                if ((now - a->utc) >= timeout)
                    to_remove.push_back(a);
            }
            for (auto* p : to_remove) {
                idle_.erase(p);
                p->Release();
                auto it = c_.find(static_cast<K>(p->key));
                if (it != c_.end())
                    c_.erase(it);
            }
        }

        struct PoolSnapshot {
            std::vector<size_t> all_keys;
            std::vector<size_t> idle_keys;
            std::vector<size_t> busy_keys;
            int idle_count;
            int busy_count;
            int total_count;
            size_t next_key;
        };

        PoolSnapshot GetSnapshot() {
            std::lock_guard<std::mutex> _(plock_);
            PoolSnapshot s{};
            s.idle_count = static_cast<int>(idle_.size());
            s.busy_count = static_cast<int>(busy_.size());
            s.total_count = static_cast<int>(c_.size());
            s.next_key = autoincrement_ + 1;
            s.all_keys.reserve(c_.size());
            for (auto& kv : c_)
                s.all_keys.push_back(kv.second->key);
            s.idle_keys.reserve(idle_.size());
            for (auto* p : idle_)
                s.idle_keys.push_back(p->key);
            s.busy_keys.reserve(busy_.size());
            for (auto* p : busy_)
                s.busy_keys.push_back(p->key);
            return s;
        }

        void ConfigureHotness(int min_idle, int max_total, int refill_batch, int shrink_batch, int64_t idle_timeout_seconds,
            double high_busy_ratio = 0.0, double low_busy_ratio = 0.0)
        {
            std::lock_guard<std::mutex> _(plock_);
            hotcfg_.min_idle = min_idle;
            hotcfg_.max_total = max_total;
            hotcfg_.refill_batch = refill_batch;
            hotcfg_.shrink_batch = shrink_batch;
            hotcfg_.idle_timeout = idle_timeout_seconds;
            hotcfg_.high_busy_ratio = high_busy_ratio;
            hotcfg_.low_busy_ratio = low_busy_ratio;
        }

        void MaintainHotness() {
            // 避免长期持锁：分段读配置+统计
            PoolStats s = GetStats();
            HotConfig cfg;
            {
                std::lock_guard<std::mutex> _(plock_);
                cfg = hotcfg_;
            }
            if (cfg.min_idle > 0 && s.idle_count < cfg.min_idle)
                EnsureMinIdle(cfg.min_idle, cfg.refill_batch);

            if (cfg.idle_timeout > 0)
                ReleaseIdle(cfg.idle_timeout);

            if (cfg.max_total > 0) {
                s = GetStats();
                if (s.total_count > cfg.max_total) {
                    int excess = s.total_count - cfg.max_total;
                    int to_shrink = cfg.shrink_batch > 0 ? std::min(cfg.shrink_batch, excess) : excess;
                    ShrinkIdleByCount(to_shrink);
                }
            }

            s = GetStats();
            if (s.total_count > 0) {
                double ratio = static_cast<double>(s.busy_count) / static_cast<double>(s.total_count);
                if (cfg.high_busy_ratio > 0.0 && ratio >= cfg.high_busy_ratio) {
                    EnsureMinIdle(std::max(cfg.min_idle, s.busy_count / 2), cfg.refill_batch);
                }
                else if (cfg.low_busy_ratio > 0.0 && ratio <= cfg.low_busy_ratio) {
                    ShrinkIdleByCount(cfg.shrink_batch);
                }
            }
        }

        void RemoveOb(pob* o) {
            if (o == nullptr) return;
            std::lock_guard<std::mutex> _(plock_);
            idle_.erase(o);
            busy_.erase(o);
            auto it = c_.find(static_cast<K>(o->key));
            if (it != c_.end())
                c_.erase(it);
        }

        void RemoveKey(size_t key) {
            T* t = FindKey(static_cast<K>(key));
            RemoveOb(t);
        }

        void BackTo(pob* o) {
            if (o == nullptr) return;
            std::lock_guard<std::mutex> _(plock_);
            o->Release();
            busy_.erase(o);
            idle_.insert(o);
        }

        void DeInitialize() {
            std::lock_guard<std::mutex> _(plock_);
            idle_.clear();
            busy_.clear();
            c_.clear();
        }
    };

}; // namespace AssetLibrary