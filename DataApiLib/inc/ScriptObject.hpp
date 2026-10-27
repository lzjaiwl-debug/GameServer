#pragma once

namespace ApiLibrary {
    // ScriptObject：脚本执行对象。
    // 继承 EscValue 与 EscHandle，既可作为脚本对象容器，也可作为脚本调用入口。
    class ScriptObject : public EscValue, public EscHandle
    {
        typedef ScriptObject CLASSNAME;
    public:
        // 构造：注册内建基础方法（如 goto）。
        ScriptObject();
        // 析构：当前无额外资源释放逻辑。
        ~ScriptObject();

        // 重置脚本环境：
        // 1) 清空对象映射与命名空间函数表；
        // 2) 清空 globals_/self_；
        // 3) 重新注册 ScriptObject 内建原生方法。
        void Reset()
        {
            {
                std::unique_lock<std::shared_mutex> lock(map_mutex_);
                EscValue::SetEmptyMap();
                ns_funcs_.Clear();
            }
            {
                std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
                globals_.Clear();
                self_ = EscValue();
                // 重新注册 ScriptObject 自身的基础原生方法。
                this->Escape("goto(...)" , THISBACK(Goto));
            }
        }

        // 从脚本文件加载：解析 lambda 并注册到对象映射、命名空间映射和 globals_。
        void LoadScript(const String& scriptfile)
        {
            scriptfile_ = scriptfile;
            String script = LoadFile(scriptfile);
            String ns = GetFileTitle(scriptfile); // 用文件名（不含扩展）作为命名空间名
            try {
                CParser p(script, GetFileName(scriptfile));
                Vector<String> ids;
                Vector<EscValue> fns;
                while (!p.IsEof()) {
                    String id = p.ReadId();
                    EscValue fn = ReadLambda(p);
                    ids.Add(id);
                    fns.Add(fn);
                }
                {
                    std::unique_lock<std::shared_mutex> lock(map_mutex_);
					for (int i = 0; i < ids.GetCount(); ++i) {
                        this->MapSet(ids[i], fns[i]); // 注册到对象映射
                        ns_funcs_.GetAdd(ns).GetPut(ids[i]) = fns[i]; // 注册到文件命名空间
					}
                }
                {
                    std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
                    for (int i = 0; i < ids.GetCount(); ++i) {
                            globals_.GetPut(ids[i]) = fns[i]; // 同步到全局（不带命名空间）
						EscValue& nsval = globals_.GetPut(ns);
						if (nsval.IsVoid())
							nsval.SetEmptyMap();
                        nsval.MapSet(ids[i], fns[i]); // 同步到 globals_[ns] 的命名空间映射
                    }
                }
            }
            catch (CParser::Error& e) {
                throw CParser::Error(e);
            }
        }

        // 从脚本文本加载（适合内嵌脚本或运行时动态注入）。
        // ext 用于错误定位显示；ns 为注入到 ns_funcs_/globals_ 的命名空间名。
        void LoadScriptFromString(const String& script, const String& ext = "esc", const String& ns = "inline")
        {
            try {
                CParser p(script, ext);
                Vector<String> ids;
                Vector<EscValue> fns;
                while (!p.IsEof()) {
                    String id = p.ReadId();
                    EscValue fn = ReadLambda(p);
                    ids.Add(id);
                    fns.Add(fn);
                }
                {
                    std::unique_lock<std::shared_mutex> lock(map_mutex_);
                    for (int i = 0; i < ids.GetCount(); ++i) {
                        this->MapSet(ids[i], fns[i]);
						ns_funcs_.GetAdd(ns).GetPut(ids[i]) = fns[i];
                    }
                }
				{
					std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
					for (int i = 0; i < ids.GetCount(); ++i) {
						globals_.GetPut(ids[i]) = fns[i];
						EscValue& nsval = globals_.GetPut(ns);
						if (nsval.IsVoid())
							nsval.SetEmptyMap();
						nsval.MapSet(ids[i], fns[i]);
					}
				}
            }
            catch (CParser::Error& e) {
                throw CParser::Error(e);
            }
        }

        // 注册原生 C++ 回调（EscEscape 形式）。
        // 可同时注册到全局与指定命名空间。
        void RegisterNative(const char* signature, CallbackN<EscEscape&> cb, const String& ns = String())
        {
			EscValue nsfn;
            {
                std::unique_lock<std::shared_mutex> lock(map_mutex_);
                this->Escape(signature, cb);
                if (!IsNull(ns))
                {
                    auto& ns_funcs_map = ns_funcs_.GetAdd(ns);
					Upp::Escape(ns_funcs_map, signature, cb);
                    String name = signature;
                    int p = name.Find('(');
                    if (p >= 0) name = name.Mid(0, p);
                    int idx = ns_funcs_map.Find(name);
					if (idx >= 0)
						nsfn = ns_funcs_map[idx];
                }
            }
            {
                std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
                Upp::Escape(globals_, signature, cb);
				if (!IsNull(ns) && !nsfn.IsVoid()) {
					EscValue& nsval = globals_.GetPut(ns);
					if (nsval.IsVoid())
						nsval.SetEmptyMap();
					String name = signature;
					int p = name.Find('(');
					if (p >= 0) name = name.Mid(0, p);
					nsval.MapSet(name, nsfn);
					ASSERT(nsval.IsMap());
				}
            }
        }

        // 以 std::function<Vector<EscValue> -> EscValue> 的形式注册原生函数。
        // 内部会自动包装成 EscEscape 回调，并支持命名空间注册。
        void RegisterNativeFn(const char* signature, std::function<EscValue(const Vector<EscValue>&)> fn, const String& ns = String())
        {
            auto wrapper = [=](EscEscape& e) {
                Vector<EscValue> args;
                args.Reserve(e.GetCount() > 1 ? e.GetCount() - 1 : 0);
                for (int i = 1; i < e.GetCount(); ++i)
                    args.Add(e[i]);
                EscValue ret = fn(args);
                e = ret;
            };
			EscValue nsfn;
            {
                std::unique_lock<std::shared_mutex> lock(map_mutex_);
                this->Escape(signature, wrapper);
                if (!IsNull(ns))
                {
                    auto& ns_funcs_map = ns_funcs_.GetAdd(ns);
                    Upp::Escape(ns_funcs_map, signature, wrapper);
					int idx = ns_funcs_map.Find(signature);
					if (idx >= 0)
						nsfn = ns_funcs_map[idx];
                }
            }
            {
                std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
                Upp::Escape(globals_, signature, wrapper);
				if (!IsNull(ns) && !nsfn.IsVoid()) {
					EscValue& nsval = globals_.GetPut(ns);
					if (nsval.IsVoid())
						nsval.SetEmptyMap();
					String name = signature;
					int p = name.Find('(');
					if (p >= 0) name = name.Mid(0, p);
					nsval.MapSet(name, nsfn);
				}
            }
        }

        // 批量导出：把当前对象映射中的所有键值复制到 globals_。
        // 先在 map_mutex_ 下做快照，再在 exec_mutex_ 下写入 globals_。
        void ExportToGlobals()
        {
            Vector<String> keys;
            Vector<EscValue> vals;
            {
                std::shared_lock<std::shared_mutex> lock(map_mutex_);
                int n = GetCount();
                keys.Reserve(n);
                vals.Reserve(n);
                for (int i = 0; i < n; ++i) {
                    String k = GetKey(i);
                    keys.Add(k);
                    vals.Add(MapGet(k));
                }
            }
            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            for (int i = 0; i < keys.GetCount(); ++i)
                globals_.GetPut(keys[i]) = vals[i];
        }

        // 加载 ESC 标准库到 globals_。
        void LoadStdLib()
        {
            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            StdLib(globals_);
        }

        // 扫描脚本文本并直接执行注册到 globals_（与 U++ 典型用法一致）。
        void ScanToGlobals(const String& scriptText, const String& filename = "inline")
        {
            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            Scan(globals_, scriptText, filename);
        }

        // 合并另一个 ScriptObject：
        // 1) 在 other 的读锁下抓取键值与命名空间快照；
        // 2) 在当前对象写锁下写入，避免并发修改冲突。
        void MergeFrom(const ScriptObject& other)
        {
            // 自合并无意义，直接返回。
            if (this == &other)
                return;

            Vector<String> keys;
            Vector<EscValue> vals;
            Vector<String> ns_names;
            Vector<Vector<String>> ns_keys;
            Vector<Vector<EscValue>> ns_vals;
            {
                // 先读取 other 的快照，避免长时间持有当前对象锁。
                std::shared_lock<std::shared_mutex> other_lock(other.map_mutex_);
                int n = other.GetCount();
                keys.Reserve(n);
                vals.Reserve(n);
                for (int i = 0; i < n; ++i) {
                    String k = other.GetKey(i);
                    keys.Add(k);
                    vals.Add(other.MapGet(k));
                }

                int ns_count = other.ns_funcs_.GetCount();
                ns_names.Reserve(ns_count);
                ns_keys.SetCount(ns_count);
                ns_vals.SetCount(ns_count);
                for (int i = 0; i < ns_count; ++i) {
                    const String& ns = other.ns_funcs_.GetKey(i);
                    const ArrayMap<String, EscValue>& src_map = other.ns_funcs_.Get(ns);
                    ns_names.Add(ns);

                    Vector<String>& keys_snapshot = ns_keys[i];
                    Vector<EscValue>& vals_snapshot = ns_vals[i];
                    keys_snapshot.Reserve(src_map.GetCount());
                    vals_snapshot.Reserve(src_map.GetCount());
                    for (int j = 0; j < src_map.GetCount(); ++j) {
                        // 分别保存函数名与函数值，避免 ArrayMap 整体拷贝限制。
                        keys_snapshot.Add(src_map.GetKey(j));
                        vals_snapshot.Add(src_map.GetValues()[j]);
                    }
                }
            }

            std::unique_lock<std::shared_mutex> lock(map_mutex_);
            for (int i = 0; i < keys.GetCount(); ++i)
                this->MapSet(keys[i], vals[i]);

            // 合并命名空间
            for (int i = 0; i < ns_names.GetCount(); ++i) {
                const String& ns = ns_names[i];
                ArrayMap<String, EscValue>& dst = ns_funcs_.Get(ns);
                const Vector<String>& keys_snapshot = ns_keys[i];
                const Vector<EscValue>& vals_snapshot = ns_vals[i];
                for (int j = 0; j < keys_snapshot.GetCount(); ++j)
                    dst.GetPut(keys_snapshot[j]) = vals_snapshot[j];
            }
        }

        // 获取当前对象映射中的所有键（函数名/变量名）。
        Vector<String> GetKeys() const
        {
            std::shared_lock<std::shared_mutex> lock(map_mutex_);
            Vector<String> keys;
            int n = GetCount();
            keys.Reserve(n);
            for (int i = 0; i < n; ++i)
                keys.Add(GetKey(i));
            return keys;
        }

        // 判断对象映射中是否存在指定名称。
        bool Has(const String& name) const
        {
            std::shared_lock<std::shared_mutex> lock(map_mutex_);
            int n = GetCount();
            for (int i = 0; i < n; ++i) {
                if (GetKey(i) == name) return true;
            }
            return false;
        }
        // 读取对象映射中的指定值；不存在时返回 Void EscValue。
        EscValue Get(const String& name) const
        {
            std::shared_lock<std::shared_mutex> lock(map_mutex_);
            return MapGet(name);
        }

        // 按名称执行脚本函数：
        // - 不带命名空间（无 '::' 或 '.'）：优先从对象映射查找，随后回退到 globals_；
        // - 带命名空间（Class::Func / Class.Func）：在 ns_funcs_[Class] 查找。
        // 执行阶段在 exec_mutex_ 下串行化，保证同一 ScriptObject 不会并发执行脚本 VM。
        EscValue Call(const String& name, Vector<EscValue>& args, int op_limit = 0)
        {
            String cls, fn;
            SplitQualified(name, cls, fn);

            EscValue target;
            if (IsNull(cls)) {
                // 无命名空间：先查对象映射（map）。
                {
                    std::shared_lock<std::shared_mutex> lock(map_mutex_);
                    target = MapGet(fn);
                }
            } else {
                // 有命名空间：查 ns_funcs_ 对应的函数表。
                std::shared_lock<std::shared_mutex> lock(map_mutex_);
                int idx = ns_funcs_.Find(cls);
                if (idx >= 0) {
                    const ArrayMap<String, EscValue>& nsmap = ns_funcs_.Get(cls);
                    int fidx = nsmap.Find(fn);
                    if (fidx >= 0) target = nsmap.Get(fn);
                }
            }

            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            // 若对象映射未命中，再从 globals_ 回退查找。
            if (IsNull(cls) && !target.IsLambda() && globals_.Find(fn) >= 0)
                target = globals_.Get(fn);

            if (!target.IsLambda())
                throw CParser::Error(Format("Script '%s' not callable or not found.", ~name));

            return Execute(globals_, &self_, target, args, op_limit);
        }

        // 变参调用：可显式传入 op_limit。
        template <typename... Args>
        EscValue CallVar(const String& name, int op_limit, Args&&... args)
        {
            Vector<EscValue> a = PackArgs(std::forward<Args>(args)...);
            return Call(name, a, op_limit);
        }

        // 变参调用：使用默认 op_limit（0）。
        template <typename... Args>
        EscValue CallVar(const String& name, Args&&... args)
        {
            Vector<EscValue> a = PackArgs(std::forward<Args>(args)...);
            return Call(name, a, 0);
        }

        // 注册已实例化对象（裸指针）的成员方法，可选绑定到命名空间。
        template <typename T>
        void RegisterInstance(const char* signature, T* instance, void (T::* method)(EscEscape&), const String& ns = String())
        {
			EscValue nsfn;
            {
                std::unique_lock<std::shared_mutex> lock(map_mutex_);
                this->Escape(signature, [instance, method](EscEscape& e) {
                    (instance->*method)(e);
                });
                if (!IsNull(ns)) {
                    auto& ns_funcs_map = ns_funcs_.GetAdd(ns);
                    auto wrapper = [instance, method](EscEscape& e) {
                        (instance->*method)(e);
                    };
                    Upp::Escape(ns_funcs_map, signature, wrapper);
					int idx = ns_funcs_map.Find(signature);
					if (idx >= 0)
						nsfn = ns_funcs_map[idx];
                }
            }
            {
                std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
                Upp::Escape(globals_, signature, [instance, method](EscEscape& e) {
                    (instance->*method)(e);
                });
				if (!IsNull(ns) && !nsfn.IsVoid()) {
					EscValue& nsval = globals_.GetPut(ns);
					if (nsval.IsVoid())
						nsval.SetEmptyMap();
					String name = signature;
					int p = name.Find('(');
					if (p >= 0) name = name.Mid(0, p);
					nsval.MapSet(name, nsfn);
				}
            }
        }

        // 注册已实例化对象（shared_ptr）的成员方法。
        // 内部用 weak_ptr 防止对象释放后悬空调用。
        template <typename T>
        void RegisterInstance(const char* signature, const std::shared_ptr<T>& instance, void (T::* method)(EscEscape&), const String& ns = String())
        {
            std::weak_ptr<T> weak = instance;
			EscValue nsfn;
            {
                std::unique_lock<std::shared_mutex> lock(map_mutex_);
                this->Escape(signature, [weak, method](EscEscape& e) {
                    if (auto p = weak.lock())
                        (p.get()->*method)(e);
                });
				if (!IsNull(ns)) {
					auto& ns_funcs_map = ns_funcs_.GetAdd(ns);
                    auto wrapper = [weak, method](EscEscape& e) {
                        if (auto p = weak.lock())
                            (p.get()->*method)(e);
                    };
                    Upp::Escape(ns_funcs_map, signature, wrapper);
					int idx = ns_funcs_map.Find(signature);
					if (idx >= 0)
						nsfn = ns_funcs_map[idx];
                }
            }
            {
                std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
                Upp::Escape(globals_, signature, [weak, method](EscEscape& e) {
                    if (auto p = weak.lock())
                        (p.get()->*method)(e);
                });
				if (!IsNull(ns) && !nsfn.IsVoid()) {
					EscValue& nsval = globals_.GetPut(ns);
					if (nsval.IsVoid())
						nsval.SetEmptyMap();
					String name = signature;
					int p = name.Find('(');
					if (p >= 0) name = name.Mid(0, p);
					nsval.MapSet(name, nsfn);
				}
            }
        }

        // 设置脚本执行上下文中的 self 对象。
        void SetSelf(const EscValue& v)
        {
            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            self_ = v;
        }

        // 获取 self 的线程局部快照。
        // 返回 const 引用以保持旧接口，但引用对象是 thread_local 副本。
        const EscValue& GetSelf()
        {
            thread_local EscValue tls_self;
            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            tls_self = self_;
            return tls_self;
        }

        // 写入（或覆盖）全局变量。
        void PutGlobal(const String& name, const EscValue& v)
        {
            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            globals_.GetPut(name, v);
        }

        // 获取 globals_ 的线程局部快照。
        // 返回 const 引用以兼容旧接口，底层为 thread_local 副本。
        const ArrayMap<String, EscValue>& GetGlobals()
        {
            thread_local ArrayMap<String, EscValue> tls_globals;
            std::lock_guard<std::recursive_mutex> exec_lock(exec_mutex_);
            tls_globals.Clear();
            tls_globals.Reserve(globals_.GetCount());
            for (int i = 0; i < globals_.GetCount(); ++i)
                tls_globals.GetPut(globals_.GetKey(i)) = globals_.GetValues()[i];
            return tls_globals;
        }

        // 判断是否存在指定命名空间。
        bool GetNamespace(const String& ns)
        {
            std::shared_lock<std::shared_mutex> lock(map_mutex_);
            return ns_funcs_.Find(ns) != -1;
		}

        // 获取指定命名空间下的所有函数名。
        Vector<String> GetNamespaceFunctions(const String& ns)
        {
            std::shared_lock<std::shared_mutex> lock(map_mutex_);
            Vector<String> fns;
            int idx = ns_funcs_.Find(ns);
            if (idx >= 0) {
                const ArrayMap<String, EscValue>& nsmap = ns_funcs_.Get(ns);
                for (int i = 0; i < nsmap.GetCount(); ++i)
                    fns.Add(nsmap.GetKey(i));
            }
            return fns;
		}
    private:
        // 最近一次加载的脚本文件路径。
        String scriptfile_;
        // 脚本执行使用的全局变量表（Execute 的 global 参数）。
        ArrayMap<String, EscValue> globals_;
        // 脚本执行使用的 self 对象（Execute 的 self 参数）。
        EscValue self_;
        // 命名空间函数表：命名空间名 -> (函数名 -> 函数 EscValue)
        ArrayMap<String, ArrayMap<String, EscValue>> ns_funcs_;

        // 保护对象映射与 ns_funcs_ 的读写锁。
        mutable std::shared_mutex map_mutex_;
        // 保护 globals_/self_ 与脚本执行过程的互斥锁（可重入）。
        mutable std::recursive_mutex exec_mutex_;

        // 内建占位方法：当前仅收集参数，不执行跳转逻辑。
        void Goto(EscEscape& e)
        {
            Vector<EscValue> args;
            for (int i = 1; i < e.GetCount(); i++)
                args.Add(e[i]);
        }

        // 从对象映射按索引取键名。
        String GetKey(int index) const
        {
            if (!IsMap())
                return String();
            return String(GetMap().GetKey(index));
        }

        // 从对象映射按键取值；不存在返回 Void EscValue。
        EscValue MapGet(const String& key) const
        {
            return EscValue::MapGet(key);
        }

        // 解析“类名.函数”或“类名::函数”；返回 cls空 表示全局
        static void SplitQualified(const String& name, String& cls, String& fn)
        {
            int pos = name.Find("::");
            int dot = name.Find('.');
            int sep = (pos >= 0 ? pos : dot);
            if (sep < 0) { cls.Clear(); fn = name; return; }
            cls = name.Mid(0, sep);
            fn = name.Mid(sep + (pos >= 0 ? 2 : 1));
        }

        // 将任意参数包封装为 Vector<EscValue>，供 Call/Execute 使用。
        template <typename... A>
        static Vector<EscValue> PackArgs(A&&... a)
        {
            Vector<EscValue> v;
            v.Reserve(sizeof...(A));
            (v.Add(EscValue(std::forward<A>(a))), ...);
            return v;
        }
    };

    // 构造实现：初始化基类并注册内建 goto 方法。
    inline ScriptObject::ScriptObject() : EscValue(), EscHandle()
    {
        std::unique_lock<std::shared_mutex> lock(map_mutex_);
        this->Escape("goto(...)", THISBACK(Goto));
    }

    // 析构实现：当前无需显式清理，资源由成员自动释放。
    inline ScriptObject::~ScriptObject() {}
};
