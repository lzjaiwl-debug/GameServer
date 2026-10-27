#ifndef _NetLibrary_PoolLib_h_
#define _NetLibrary_PoolLib_h_
namespace NetLibrary {
	struct PoolParent : public Moveable<PoolParent>
{
    typedef PoolParent PClass;
    size_t linkid = -1;
    size_t key = -1;
    int64 utc = -1;
    
    template <typename T>
    T* GetClass() {
        return dynamic_cast<T*>(this);
    };
    
    PoolParent() {};
    virtual ~PoolParent() { 
        linkid = key = utc = -1; 
    };
    virtual void Release() = 0;
};

template <typename T,class K>
class PoolLib
{
    static_assert(std::is_base_of_v<PoolParent, T>, 
                  "Template parameter T must inherit from PoolParent");
	//typedef PoolLib<T,K> CLASSNAME;
	typedef typename T::PClass pob;
private:
	VectorMap<K,One<pob>> c;
	Index<pob*> Idle;
	Index<pob*> Busy;
	size_t Autoincrement = 0;
	Mutex plock;
    // 热度控制配置
    struct HotConfig {
        int min_idle = 0;          // 低水位：最少空闲对象
        int max_total = 0;         // 高水位：最大总对象数（0=不限制）
        int refill_batch = 0;      // 批量补充个数（0=按需）
        int shrink_batch = 0;      // 批量收缩个数（0=按需）
        int64 idle_timeout = 0;    // 空闲超时销毁秒数（0=不销毁）
        double high_busy_ratio = 0; // 自适应：高忙比阈值（0=禁用）
        double low_busy_ratio = 0;  // 自适应：低忙比阈值（0=禁用）
    } hotcfg;
	// 内部辅助方法
    T* CreatNewOneUnlocked(bool is_busy) {
        Autoincrement++;
        One<pob> nt = new T(Autoincrement);
        nt->key = Autoincrement;  // 确保设置key
        nt->utc = GetUTCSeconds(GetSysTime());
        
        T* t = c.Add(Autoincrement, pick(nt))->template GetClass<T>();
        if(is_busy)
            Busy.Put(t);
        else
            Idle.Put(t);
        return t;
    }
public:
	PoolLib(){};
	~PoolLib(){
		Clear();
	};
	void Clear() {
        Mutex::Lock __(plock);
        c.Clear();
        Idle.Clear();
        Busy.Clear();
        Autoincrement = 0;
    }
	T* FindKey(K k)
	{
		Mutex::Lock __(plock);
		int pos = c.Find(k);
		return pos >= 0 ? c[pos]->template GetClass<T>() : nullptr;
	}
	T* CreatNewOne(bool is_busy = false)
	{
		Mutex::Lock __(plock);
        return CreatNewOneUnlocked(is_busy);
	}
	// 批量补充空闲对象
	void RefillIdle(int count)
	{
		if(count <= 0) return;
		Mutex::Lock __(plock);
		for(int i = 0; i < count; i++)
			CreatNewOneUnlocked(false);
	}
	// 确保空闲池至少有 min_idle 个对象；batch>0 时按批量补充
	void EnsureMinIdle(int min_idle, int batch = 0)
	{
		if(min_idle <= 0) return;
		Mutex::Lock __(plock);
		int need = min_idle - Idle.GetCount();
		if(need <= 0) return;
		int to_create = need;
		if(batch > 0 && batch > need)
			to_create = batch;
		for(int i = 0; i < to_create; i++)
			CreatNewOneUnlocked(false);
	}

    // 按数量从空闲集中删除最老的对象（真正释放）
    void ShrinkIdleByCount(int count)
    {
        if(count <= 0) return;
        Mutex::Lock __(plock);
        for(int k = 0; k < count && Idle.GetCount() > 0; ++k)
        {
            pob* oldest = NULL;
            int64 oldest_utc = 0;
            bool has = false;
            for(auto& a : Idle) {
                if(!has || a->utc < oldest_utc) { oldest = a; oldest_utc = a->utc; has = true; }
            }
            if(!oldest) break;
            Idle.RemoveKey(oldest);
            oldest->Release();
            if(c.Find(oldest->key) != -1)
                c.RemoveKey(oldest->key);
        }
    }
	void CreatNewList(int Num)
	{
		Mutex::Lock __(plock);
		for(int i = 0;i<Num;i++)
		{
			CreatNewOneUnlocked(false);
		}
	}
	void ReleaseOb(pob* o)
	{
		BackTo(o);
	}
	T* TakeOut(bool isadd = false)
	{
		Mutex::Lock __(plock);
		if(Idle.GetCount()>0)
		{
			pob* p = Idle.Pop();
			p->utc = GetUTCSeconds(GetSysTime());
			T* t = p->template GetClass<T>();
			Busy.Put(p);
	        return t;
		}
		if(isadd)
			return CreatNewOneUnlocked(true);
		return NULL;
	}
	 // 获取统计信息（线程安全）
    struct PoolStats {
        int idle_count;
        int busy_count;
        int total_count;
        size_t next_key;
    };
    
    PoolStats GetStats() {
        Mutex::Lock __(plock);
        return {
            Idle.GetCount(),
            Busy.GetCount(),
            c.GetCount(),
            Autoincrement + 1
        };
    }
	VectorMap<K,One<pob>>& GetListOb()
	{
		return c;
	}
	const VectorMap<K,One<pob>>& GetListOb() const
	{
		return c;
	}
	const Index<pob*>& GetIdle()
	{
		return Idle;
	}
	const Index<pob*>& GetIdle() const
	{
		return Idle;
	}
	int GetIdleNum()
	{
		Mutex::Lock __(plock);
		return Idle.GetCount();
	}
	int GetBusyNum()
	{
		Mutex::Lock __(plock);
		return Busy.GetCount();
	}
	int GetAllNum()
	{
		Mutex::Lock __(plock);
		return c.GetCount();
	}
	T* FindLinkId(int id)
	{
		Mutex::Lock __(plock);
		T* tp = NULL;
		for(auto& a : c)
		{
			if(a->linkid == id)
				tp = a->template GetClass<T>();
		}
		return tp;
	}
	// 超时销毁空闲对象（真正释放而非迁移）
	void ReleaseIdle(int64 timeout)
	{
		int64 now = GetUTCSeconds(GetSysTime());
		Mutex::Lock __(plock);
		Vector<pob*> to_remove;
		for(auto& a : Idle)
		{
			if((now - a->utc) >= timeout)
				to_remove.Add(a);
		}
		for(auto& p : to_remove)
		{
			Idle.RemoveKey(p);
			// 调用对象清理并从主容器删除（释放 One 拥有的内存）
			p->Release();
			if(c.Find(p->key) != -1)
				c.RemoveKey(p->key);
		}
	}

	// 只读快照接口：返回当前池中对象键的分布
	struct PoolSnapshot {
		Vector<size_t> all_keys;
		Vector<size_t> idle_keys;
		Vector<size_t> busy_keys;
		int idle_count;
		int busy_count;
		int total_count;
		size_t next_key;
	};

	PoolSnapshot GetSnapshot()
	{
		Mutex::Lock __(plock);
		PoolSnapshot s;
		s.idle_count = Idle.GetCount();
		s.busy_count = Busy.GetCount();
		s.total_count = c.GetCount();
		s.next_key = Autoincrement + 1;
		for(auto& v : c)
			s.all_keys.Add(v->key);
		for(auto& p : Idle)
			s.idle_keys.Add(p->key);
		for(auto& p : Busy)
			s.busy_keys.Add(p->key);
		return s;
	}

    // 配置热度控制参数
    void ConfigureHotness(int min_idle, int max_total, int refill_batch, int shrink_batch, int64 idle_timeout_seconds,
                          double high_busy_ratio = 0.0, double low_busy_ratio = 0.0)
    {
        Mutex::Lock __(plock);
        hotcfg.min_idle = min_idle;
        hotcfg.max_total = max_total;
        hotcfg.refill_batch = refill_batch;
        hotcfg.shrink_batch = shrink_batch;
        hotcfg.idle_timeout = idle_timeout_seconds;
        hotcfg.high_busy_ratio = high_busy_ratio;
        hotcfg.low_busy_ratio = low_busy_ratio;
    }

    // 周期性维护：水位补充/超时销毁/高水位收缩（自适应可选）
    void MaintainHotness()
    {
        // 读取一次配置和统计（避免长期持锁）
        PoolStats s = GetStats();
        HotConfig cfg;
        {
            Mutex::Lock __(plock);
            cfg = hotcfg;
        }
        // 低水位补充
        if(cfg.min_idle > 0 && s.idle_count < cfg.min_idle)
            EnsureMinIdle(cfg.min_idle, cfg.refill_batch);
        // 超时销毁
        if(cfg.idle_timeout > 0)
            ReleaseIdle(cfg.idle_timeout);
        // 高水位收缩
        if(cfg.max_total > 0) {
            s = GetStats();
            if(s.total_count > cfg.max_total) {
                int excess = s.total_count - cfg.max_total;
                int to_shrink = cfg.shrink_batch > 0 ? std::min(cfg.shrink_batch, excess) : excess;
                ShrinkIdleByCount(to_shrink);
            }
        }
        // 自适应调节（仅执行一次性动作，不修改配置）
        s = GetStats();
        if(s.total_count > 0) {
            double ratio = (double)s.busy_count / (double)s.total_count;
            if(cfg.high_busy_ratio > 0.0 && ratio >= cfg.high_busy_ratio)
                EnsureMinIdle(std::max(cfg.min_idle, s.busy_count / 2), cfg.refill_batch);
            else if(cfg.low_busy_ratio > 0.0 && ratio <= cfg.low_busy_ratio)
                ShrinkIdleByCount(cfg.shrink_batch);
        }
    }
	void RemoveOb(pob* o)
	{
		if(o == NULL)
			return;
		if(Idle.Find(o) != -1)
			Idle.RemoveKey(o);
		if(Busy.Find(o) != -1)
			Busy.RemoveKey(o);
		if(c.Find(o->key) != -1)
			c.RemoveKey(o->key);
	}
	void RemoveKey(size_t key)
	{
		T* t = FindKey(key);
		RemoveOb(t);
	}
	void BackTo(pob* o)
	{
		Mutex::Lock __(plock);
		if(o == NULL)
			return;
		o->Release();
		Busy.RemoveKey(o);
		Idle.Put(o);
	}
	void DeInitialize()
	{
        if(Idle.GetCount() != 0)
			Idle.Clear();
        if(Busy.GetCount() != 0)
			Busy.Clear();
        if(c.GetCount() != 0)
			c.Clear();
	}
};
};
#endif