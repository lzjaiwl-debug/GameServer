#pragma once

namespace AssetLib {

/// <summary>
/// 线程安全并发 Map（分桶 + 读写锁）。
///
/// 设计说明：
/// 1) 内部将数据分散到多个桶（bucket），每个桶有独立 shared_mutex。
/// 2) 读操作使用共享锁，写操作使用独占锁，提高读多写少场景吞吐。
/// 3) Size() 使用原子计数，避免全表加锁统计。
///
/// 适用场景：
/// - 百万级连接对象按 key 查找（如 fd -> UserInfo*，sid -> UserInfo*）
/// - 高并发读，低到中等并发写
///
/// 使用示例：
///   ConcurrentMap<String, int> m(1024, 1000000);
///   m.InsertOrAssign("k", 1);
///   int v = 0;
///   if (m.TryGetCopy("k", v)) {
///       // 使用 v
///   }
///
///   // 原地只读访问（共享锁）
///   m.WithValue("k", [](const int& x) {
///       // 只读处理
///   });
///
///   // 原地修改（独占锁）
///   m.Update("k", [](int& x) {
///       x += 1;
///   });
/// </summary>
template <class K, class V, class Hash = std::hash<K>, class KeyEqual = std::equal_to<K>>
class ConcurrentMap {
private:
	template <class T>
	struct IsSharedPtr : std::false_type {};
	template <class U>
	struct IsSharedPtr<std::shared_ptr<U>> : std::true_type {};

	struct Bucket {
		/// 每个桶独立锁，降低全局锁竞争
		mutable std::shared_mutex mtx;
		std::unordered_map<K, V, Hash, KeyEqual> map;

		Bucket() = default;
		Bucket(const Bucket&) = delete;
		Bucket& operator=(const Bucket&) = delete;

		// shared_mutex 不可复制/移动；这里自定义“可移动桶”：
		// 仅迁移 map，目标桶使用自己的新互斥量。
		Bucket(Bucket&& other) noexcept
			: map(std::move(other.map)) {
		}

		Bucket& operator=(Bucket&& other) noexcept {
			if (this != &other) {
				std::unique_lock<std::shared_mutex> lk(mtx);
				map = std::move(other.map);
			}
			return *this;
		}
	};

	std::vector<Bucket> buckets_;
	Hash hasher_;
	std::atomic<size_t> size_{ 0 };

	size_t BucketIndex(const K& key) const {
		return hasher_(key) % buckets_.size();
	}

	// 将值类型转换为原始指针（支持原始指针 / shared_ptr / unique_ptr）
	template <class TPtr>
	static auto ToRawPtr(TPtr& p) -> decltype(p.get()) { return p.get(); }
	template <class TPtr>
	static auto ToRawPtr(const TPtr& p) -> decltype(p.get()) { return p.get(); }
	template <class U>
	static U* ToRawPtr(U* p) { return p; }
	template <class U>
	static const U* ToRawPtr(const U* p) { return p; }

	template <class To, class From, std::enable_if_t<std::is_polymorphic_v<From>, int> = 0>
	static To* CastRaw(From* p) {
		return dynamic_cast<To*>(p);
	}

	template <class To, class From, std::enable_if_t<!std::is_polymorphic_v<From>, int> = 0>
	static To* CastRaw(From* p) {
		return static_cast<To*>(p);
	}

	template <class To, class From, std::enable_if_t<std::is_polymorphic_v<From>, int> = 0>
	static std::shared_ptr<To> CastShared(const std::shared_ptr<From>& p) {
		return std::dynamic_pointer_cast<To>(p);
	}

	template <class To, class From, std::enable_if_t<!std::is_polymorphic_v<From>, int> = 0>
	static std::shared_ptr<To> CastShared(const std::shared_ptr<From>& p) {
		return std::static_pointer_cast<To>(p);
	}

	static size_t NormalizeBucketCount(size_t n) {
		if (n < 1) n = 1;
		size_t p = 1;
		while (p < n) p <<= 1;
		return p;
	}

public:
	/// <summary>
	/// 构造并发 Map。
	/// </summary>
	/// <param name="bucket_count">桶数量（会归一化到 2 的幂，建议按并发度设置，如 256/512/1024）。</param>
	/// <param name="reserve_hint">预估总元素数，用于预分配减少扩容开销。</param>
	explicit ConcurrentMap(size_t bucket_count = 256, size_t reserve_hint = 0)
	{
		bucket_count = NormalizeBucketCount(bucket_count);
		buckets_.resize(bucket_count);
		if (reserve_hint > 0) {
			size_t each = reserve_hint / bucket_count + 1;
			for (auto& b : buckets_)
				b.map.reserve(each);
		}
	}

	/// <summary>返回桶数量。</summary>
	size_t BucketCount() const { return buckets_.size(); }
	/// <summary>返回当前元素总数（原子计数）。</summary>
	size_t Size() const { return size_.load(std::memory_order_acquire); }
	/// <summary>是否为空。</summary>
	bool Empty() const { return Size() == 0; }

	/// <summary>
	/// 重建容器内部桶结构（可在构造后由配置驱动初始化）。
	///
	/// 使用约束：
	/// 1) 仅允许在无并发访问阶段调用（如服务启动期）。
	/// 2) 调用后会清空当前所有元素。
	/// </summary>
	/// <param name="bucket_count">新桶数量（会归一化到 2 的幂）。</param>
	/// <param name="reserve_hint">预估总元素数，用于预分配。</param>
	/// <returns>始终返回 true（保留返回值便于未来扩展失败分支）。</returns>
	bool Rebuild(size_t bucket_count, size_t reserve_hint = 0) {
		bucket_count = NormalizeBucketCount(bucket_count);

		std::vector<Bucket> fresh;
		fresh.resize(bucket_count);
		if (reserve_hint > 0) {
			size_t each = reserve_hint / bucket_count + 1;
			for (auto& b : fresh)
				b.map.reserve(each);
		}

		buckets_.swap(fresh);
		size_.store(0, std::memory_order_release);
		return true;
	}

	/// <summary>
	/// 预分配容量（按桶平均分配）。
	/// 建议在已知规模（如百万连接）时提前调用。
	/// </summary>
	void Reserve(size_t total_count_hint) {
		size_t each = total_count_hint / buckets_.size() + 1;
		for (auto& b : buckets_) {
			std::unique_lock<std::shared_mutex> lk(b.mtx);
			b.map.reserve(each);
		}
	}

	/// <summary>
	/// 仅在 key 不存在时插入。
	/// </summary>
	/// <returns>插入成功返回 true；若 key 已存在返回 false。</returns>
	template <class KK, class VV>
	bool Insert(KK&& key, VV&& value) {
		auto& b = buckets_[BucketIndex(key)];
		std::unique_lock<std::shared_mutex> lk(b.mtx);
		auto [it, inserted] = b.map.emplace(std::forward<KK>(key), std::forward<VV>(value));
		if (inserted)
			size_.fetch_add(1, std::memory_order_release);
		return inserted;
	}

	/// <summary>
	/// 便捷接口：按派生对象写入（典型用于 V=Base* 或 V=std::shared_ptr&lt;Base&gt;）。
	///
	/// 示例：
	///   ConcurrentMap<int, std::shared_ptr<Base>> m;
	///   m.InsertDerived(1, std::make_shared<Derived>(...));
	/// </summary>
	template <class KK, class DerivedValue,
			  class = std::enable_if_t<std::is_constructible_v<V, DerivedValue&&>>>
	bool InsertDerived(KK&& key, DerivedValue&& derived_value) {
		return InsertOrAssign(std::forward<KK>(key), std::forward<DerivedValue>(derived_value));
	}

	/// <summary>
	/// 插入或覆盖。
	/// </summary>
	/// <returns>新插入返回 true；覆盖旧值返回 false。</returns>
	template <class KK, class VV>
	bool InsertOrAssign(KK&& key, VV&& value) {
		auto& b = buckets_[BucketIndex(key)];
		std::unique_lock<std::shared_mutex> lk(b.mtx);
		auto [it, inserted] = b.map.insert_or_assign(std::forward<KK>(key), std::forward<VV>(value));
		if (inserted)
			size_.fetch_add(1, std::memory_order_release);
		return inserted;
	}

	/// <summary>
	/// 按 key 删除。
	/// </summary>
	/// <returns>删除成功返回 true；未找到返回 false。</returns>
	bool Erase(const K& key) {
		auto& b = buckets_[BucketIndex(key)];
		std::unique_lock<std::shared_mutex> lk(b.mtx);
		size_t n = b.map.erase(key);
		if (n > 0)
			size_.fetch_sub(1, std::memory_order_release);
		return n > 0;
	}

	/// <summary>
	/// 判断 key 是否存在。
	/// </summary>
	bool Contains(const K& key) const {
		const auto& b = buckets_[BucketIndex(key)];
		std::shared_lock<std::shared_mutex> lk(b.mtx);
		return b.map.find(key) != b.map.end();
	}

	/// <summary>
	/// 尝试按 key 获取值副本。
	/// </summary>
	/// <param name="out">找到时写入副本。</param>
	/// <returns>找到返回 true，否则 false。</returns>
	bool TryGetCopy(const K& key, V& out) const {
		const auto& b = buckets_[BucketIndex(key)];
		std::shared_lock<std::shared_mutex> lk(b.mtx);
		auto it = b.map.find(key);
		if (it == b.map.end())
			return false;
		out = it->second;
		return true;
	}

	/// <summary>
	/// 尝试按派生类型获取值副本。
	///
	/// 支持场景：
	/// 1) V = Base*，DerivedValue = Derived*（dynamic_cast）
	/// 2) V = std::shared_ptr<Base>，DerivedValue = std::shared_ptr<Derived>（dynamic_pointer_cast）
	/// </summary>
	template <class DerivedValue>
	bool TryGetCopyAs(const K& key, DerivedValue& out) const {
		const auto& b = buckets_[BucketIndex(key)];
		std::shared_lock<std::shared_mutex> lk(b.mtx);
		auto it = b.map.find(key);
		if (it == b.map.end())
			return false;

		if constexpr (std::is_pointer_v<V> && std::is_pointer_v<DerivedValue>) {
			auto* p = CastRaw<std::remove_pointer_t<DerivedValue>>(it->second);
			if (!p) return false;
			out = p;
			return true;
		}
		else if constexpr (IsSharedPtr<V>::value && IsSharedPtr<DerivedValue>::value) {
			auto p = CastShared<typename DerivedValue::element_type>(it->second);
			if (!p) return false;
			out = std::move(p);
			return true;
		}
		else {
			return false;
		}
	}

	/// <summary>
	/// 便捷接口：按派生类型更新。
	///
	/// 仅适用于 V 为“指针类型”（如 Base* / shared_ptr&lt;Base&gt; / unique_ptr&lt;Base&gt;）。
	/// 多态基类时使用 dynamic_cast；非多态基类时使用 static_cast（由调用方保证类型匹配）。
	///
	/// 回调支持两种签名：
	/// 1) fn(Derived&)  
	/// 2) fn(Derived*)
	/// </summary>
	template <class Derived, class Fn>
	bool UpdateAs(const K& key, Fn&& fn) {
		auto& b = buckets_[BucketIndex(key)];
		std::unique_lock<std::shared_mutex> lk(b.mtx);
		auto it = b.map.find(key);
		if (it == b.map.end())
			return false;

		auto* base_ptr = ToRawPtr(it->second);
		if (!base_ptr)
			return false;

		auto* d = CastRaw<Derived>(base_ptr);
		if (!d)
			return false;

		if constexpr (std::is_invocable_v<Fn&, Derived&>)
			fn(*d);
		else
			fn(d);
		return true;
	}

	/// <summary>
	/// 在共享锁下访问值（只读或轻量处理）。
	/// 注意：回调内不应长时间阻塞。
	/// </summary>
	template <class Fn>
	bool WithValue(const K& key, Fn&& fn) const {
		const auto& b = buckets_[BucketIndex(key)];
		std::shared_lock<std::shared_mutex> lk(b.mtx);
		auto it = b.map.find(key);
		if (it == b.map.end())
			return false;
		fn(it->second);
		return true;
	}

	/// <summary>
	/// 在独占锁下修改值。
	/// 注意：回调内不应再访问本容器其它 key，避免复杂锁顺序。
	/// </summary>
	template <class Fn>
	bool Update(const K& key, Fn&& fn) {
		auto& b = buckets_[BucketIndex(key)];
		std::unique_lock<std::shared_mutex> lk(b.mtx);
		auto it = b.map.find(key);
		if (it == b.map.end())
			return false;
		fn(it->second);
		return true;
	}

	/// <summary>
	/// 清空所有元素。
	/// </summary>
	void Clear() {
		for (auto& b : buckets_) {
			std::unique_lock<std::shared_mutex> lk(b.mtx);
			b.map.clear();
		}
		size_.store(0, std::memory_order_release);
	}

	/// <summary>
	/// 遍历所有元素（按桶顺序）。
	/// 注意：遍历期间每个桶持有共享锁；不保证全局快照一致性。
	/// </summary>
	template <class Fn>
	void ForEach(Fn&& fn) const {
		for (const auto& b : buckets_) {
			std::shared_lock<std::shared_mutex> lk(b.mtx);
			for (const auto& kv : b.map)
				fn(kv.first, kv.second);
		}
	}

	/// <summary>
	/// 按派生类型遍历（典型用于 V=Base* 或 V=std::shared_ptr&lt;Base&gt;）。
	///
	/// 对于多态基类：
	/// - raw pointer 使用 dynamic_cast
	/// - shared_ptr 使用 dynamic_pointer_cast
	///
	/// 对于非多态基类：
	/// - raw pointer 使用 static_cast
	/// - shared_ptr 使用 static_pointer_cast
	/// </summary>
	template <class Derived, class Fn>
	void ForEachAs(Fn&& fn) const {
		for (const auto& b : buckets_) {
			std::shared_lock<std::shared_mutex> lk(b.mtx);
			for (const auto& kv : b.map) {
				if constexpr (IsSharedPtr<V>::value) {
					std::shared_ptr<Derived> p = CastShared<Derived>(kv.second);
					if (!p)
						continue;
					if constexpr (std::is_invocable_v<Fn&, const K&, const Derived&>)
						fn(kv.first, *p);
					else if constexpr (std::is_invocable_v<Fn&, const K&, const std::shared_ptr<Derived>&>)
						fn(kv.first, p);
					else
						static_assert(std::is_invocable_v<Fn&, const K&, const Derived&> || std::is_invocable_v<Fn&, const K&, const std::shared_ptr<Derived>&>,
							"ForEachAs callback must be invocable with (const K&, const Derived&) or (const K&, const std::shared_ptr<Derived>&)." );
				}
				else if constexpr (std::is_pointer_v<V>) {
					Derived* p = CastRaw<Derived>(kv.second);
					if (!p)
						continue;
					if constexpr (std::is_invocable_v<Fn&, const K&, const Derived&>)
						fn(kv.first, *p);
					else if constexpr (std::is_invocable_v<Fn&, const K&, const Derived*>)
						fn(kv.first, p);
					else
						static_assert(std::is_invocable_v<Fn&, const K&, const Derived&> || std::is_invocable_v<Fn&, const K&, const Derived*>,
							"ForEachAs callback must be invocable with (const K&, const Derived&) or (const K&, const Derived*)." );
				}
				else {
					static_assert(IsSharedPtr<V>::value || std::is_pointer_v<V>,
						"ForEachAs only supports pointer-like value types (raw pointer/shared_ptr).");
				}
			}
		}
	}
};

}