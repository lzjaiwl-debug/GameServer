#pragma once
namespace ApiLibrary {
	inline void ParseValueMap(const ValueMap& vm, EscValue& ev);
	inline void ParseValueArray(const ValueArray& va, EscValue& ev);
	inline void ParseValue(const Value& v, EscValue& ev) {
		// 按类型分支，匹配到后直接返回，避免被回退逻辑覆盖
		if (v.Is<int>()) { ev = v.Get<int>();            return; }
		if (v.Is<int64>()) { ev = v.Get<int64>();          return; } // 保留 int64
		if (v.Is<double>()) { ev = v.Get<double>();         return; }
		if (v.Is<float>()) { ev = (double)v.Get<float>();  return; } // EscValue 数字统一为 double/int64
		if (v.Is<bool>()) { ev = v.Get<bool>();           return; }
		if (v.Is<String>()) { ev = v.Get<String>();         return; }
		if (v.Is<WString>()) { ev = v.Get<WString>().ToString(); return; }
		if (v.Is<Date>()) {
			Time t = ToTime(v.Get<Date>());
			ev = GetUTCSeconds(t);
			return;
		}
		if (v.Is<Time>()) {
			Time t = v.Get<Time>();
			ev = GetUTCSeconds(t);
			return;
		}
		if (v.Is<ValueMap>()) { ParseValueMap(v, ev);   return; }
		if (v.Is<ValueArray>()) { ParseValueArray(v, ev); return; }

		// Fallback：未知类型按字符串
		ev = EscValue(v.ToString());
	}

	inline void ParseValueMap(const ValueMap& vm, EscValue& ev) {
		ev.SetEmptyMap();
		for (int i = 0; i < vm.GetCount(); i++) {
			EscValue key_ev, val_ev;
			ParseValue(vm.GetKey(i), key_ev);
			ParseValue(vm.GetValue(i), val_ev);
			ev.MapSet(key_ev, val_ev);
		}
	}

	inline void ParseValueArray(const ValueArray& va, EscValue& ev) {
		ev.SetEmptyArray();
		for (int i = 0; i < va.GetCount(); i++) {
			EscValue elem_ev;
			ParseValue(va[i], elem_ev);
			ev.ArrayAdd(elem_ev);
		}
	}
	inline void ParseEscValueArray(const Vector<EscValue>& arr, Value& v);
	inline void ParseEscValueMap(const VectorMap<EscValue, EscValue>& m, Value& v);
	inline bool IsUtf32EscArray(const EscValue& ev);
	inline String EscValueToUtf8String(const EscValue& ev);
	inline void ParseEscValue(const EscValue& ev, Value& v) {
		// 优先精确类型，再到集合
		if (ev.IsInt64()) { v = ev.GetInt64();  return; }
		if (ev.IsInt()) { v = ev.GetInt();    return; }
		if (ev.IsNumber()) { v = ev.GetNumber(); return; }
		if (IsUtf32EscArray(ev)) { v = EscValueToUtf8String(ev); return; }
		if (ev.IsArray()) { ParseEscValueArray(ev.GetArray(), v); return; }
		if (ev.IsMap()) { ParseEscValueMap(ev.GetMap(), v);     return; }
		// 其他一律转字符串
		v = (String)ev;
	}

	inline bool IsUtf32EscArray(const EscValue& ev)
	{
		if (!ev.IsArray())
			return false;
		const Vector<EscValue>& arr = ev.GetArray();
		if (arr.IsEmpty())
			return false;
		for (int i = 0; i < arr.GetCount(); ++i) {
			if (!arr[i].IsInt())
				return false;
		}
		return true;
	}

	inline String EscValueToUtf8String(const EscValue& ev)
	{
		if (!ev.IsArray())
			return ev.ToString();
		const Vector<EscValue>& arr = ev.GetArray();
		String out;
		for (int i = 0; i < arr.GetCount(); ++i) {
			int cp = arr[i].GetInt();
			if (cp <= 0x7F) {
				out.Cat((char)cp);
			}
			else if (cp <= 0x7FF) {
				out.Cat((char)(0xC0 | ((cp >> 6) & 0x1F)));
				out.Cat((char)(0x80 | (cp & 0x3F)));
			}
			else if (cp <= 0xFFFF) {
				out.Cat((char)(0xE0 | ((cp >> 12) & 0x0F)));
				out.Cat((char)(0x80 | ((cp >> 6) & 0x3F)));
				out.Cat((char)(0x80 | (cp & 0x3F)));
			}
			else {
				out.Cat((char)(0xF0 | ((cp >> 18) & 0x07)));
				out.Cat((char)(0x80 | ((cp >> 12) & 0x3F)));
				out.Cat((char)(0x80 | ((cp >> 6) & 0x3F)));
				out.Cat((char)(0x80 | (cp & 0x3F)));
			}
		}
		return out;
	}

	inline void ParseEscValueArray(const Vector<EscValue>& arr, Value& v) {
		ValueArray out;
		for (int i = 0; i < arr.GetCount(); i++) {
			Value item;
			ParseEscValue(arr[i], item);
			out.Add(item);
		}
		v = out;
	}

	inline void ParseEscValueMap(const VectorMap<EscValue, EscValue>& m, Value& v) {
		ValueMap out;
		for (int i = 0; i < m.GetCount(); i++) {
			Value k, val;
			EscValue key_ev = m.GetKey(i);
			if (IsUtf32EscArray(key_ev))
				k = EscValueToUtf8String(key_ev);
			else
				ParseEscValue(key_ev, k);
			ParseEscValue(m.Get(~k), val);
			out.GetAdd(k) = val;
		}
		v = out;
	}
	/// <summary>
	/// 检查给定的 EscValue 是否表示一个“EscString”：即它是一个数组且数组中所有元素均为数字（Number 或 Int64）。
	/// </summary>
	/// <param name="v">要检查的 EscValue 对象。</param>
	/// <returns>如果 v 是数组且其所有元素都为数字（IsNumber 返回 true）或 64 位整数（IsInt64 返回 true），则返回 true；否则返回 false。</returns>
	inline bool IsEscString(const EscValue& v)
	{
		if (!v.IsArray()) return false;
		const auto& a = v.GetArray();
		for (const auto& e : a) {
			if (!e.IsNumber() && !e.IsInt64())
				return false;
		}
		return true;
	}
	inline EscValue EscValueFromAny(const std::any& any)
	{
		if (any.type() == typeid(String))
			return String(std::any_cast<String>(any));
		if (any.type() == typeid(int64))
			return EscValue(std::any_cast<int64>(any));
		if (any.type() == typeid(bool))
			return EscValue(std::any_cast<bool>(any));
		if (any.type() == typeid(std::string))
			return String(std::any_cast<std::string>(any).c_str());
		if (any.type() == typeid(const char*))
			return String(std::any_cast<const char*>(any));
		if (any.type() == typeid(int))
			return EscValue(std::any_cast<int>(any));
		if (any.type() == typeid(double))
			return EscValue(std::any_cast<double>(any));
		if (any.type() == typeid(nlohmann::json)) {
			nlohmann::json j = std::any_cast<nlohmann::json>(any);
			Value v = ParseJSON(j.dump(4).c_str());
			EscValue ev; ParseValue(v, ev);
			return ev;
		}
		return EscValue();
	}
	inline std::any EscValueToAny(const EscValue& ev)
	{
		if (ev.IsInt64())
			return ev.GetInt64();
		if (ev.IsInt())
			return ev.GetInt();
		if (ev.IsNumber())
			return ev.GetNumber();
		if (IsUtf32EscArray(ev))
			return EscValueToUtf8String(ev);
		if (ev.IsArray()) {
			Value v; ParseEscValue(ev, v);
			return v;
		}
		if (ev.IsMap()) {
			Value v; ParseEscValue(ev, v);
			return v;
		}
		return String(ev.ToString());
	}
	inline void AnyListFromEscValueList(const Vector<EscValue>& args, AssetLib::EventArgs& eventArgs)
	{
		eventArgs.resize(args.size());
		for (auto& arg : args) {
			if (arg.IsArray())
			{
				if (IsEscString(arg))
					eventArgs.push_back(String(arg).ToStd());
				else
				{
					Value v; ParseEscValue(arg, v);
					nlohmann::json j = AsJSON(v, true);
					eventArgs.push_back(j);
				}
			}
			if (arg.IsInt64())
				eventArgs.push_back(arg.GetInt64());
			if (arg.IsNumber())
				eventArgs.push_back(arg.GetNumber());
			if (arg.IsMap()) {
				Value v; ParseEscValue(arg, v);
				nlohmann::json j = AsJSON(v, true);
				eventArgs.push_back(j);
			}
		}
	}
	// 通用路径解析：支持绝对/相对路径，优先基于 base 解析，其次工作目录、可执行目录，最终归一化返回
	inline String ResolvePath(const String& path, const String& base = String())
	{
		if (IsNull(path))
			return String();

		auto Try = [](const String& p) -> String {
			if (!IsNull(p) && (FileExists(~p) || DirectoryExists(~p)))
				return NormalizePath(p);
			return String();
		};

		// 1) 绝对路径直接校验
		if (IsFullPath(path)) {
			if (auto ok = Try(path); !IsNull(ok))
				return ok;
		}

		// 2) 基路径（如最近脚本目录）相对解析
		if (!IsNull(base)) {
			String baseDir = GetFileDirectory(base);
			if (!IsNull(baseDir)) {
				String p = NormalizePath(AppendFileName(baseDir, path));
				if (auto ok = Try(p); !IsNull(ok))
					return ok;
			}
		}

		// 3) 当前工作目录相对解析（含 ./ ../ 前缀）
		{
			String cwd = GetCurrentDirectory();
			String p = NormalizePath(AppendFileName(cwd, path));
			if (auto ok = Try(p); !IsNull(ok))
				return ok;
		}

		// 4) 可执行文件目录相对解析
		{
			String exeDir = GetExeFolder();
			String p = NormalizePath(AppendFileName(exeDir, path));
			if (auto ok = Try(p); !IsNull(ok))
				return ok;
		}

		// 5) 兜底：归一化后返回（可能不存在，但格式正确）
		return NormalizePath(path);
	}
	// 路径解析助手：返回可用的脚本文件完整路径；若不可用返回空字符串
	inline String ResolveScriptPath(const String& in, const String& lastScriptFile)
	{
		String candidate = in;

		auto Try = [](const String& p) -> String {
			if (!IsNull(p) && FileExists(~p))
				return p;
			return String();
		};

		// 0) 若以 ./ 或 ../ 开头，先对当前工作目录进行归一化解析
		if (candidate.StartsWith("./") || candidate.StartsWith(".\\")
			|| candidate.StartsWith("../") || candidate.StartsWith("..\\"))
		{
			String cwd = GetCurrentDirectory();
			String norm = NormalizePath(AppendFileName(cwd, candidate));
			if (auto ok = Try(norm); !IsNull(ok))
				return ok;
		}

		// 1) 原路径（绝对或相对当前工作目录）
		if (auto ok = Try(candidate); !IsNull(ok))
			return ok;

		// 2) 可执行文件目录相对路径
		{
			String exeDir = GetExeFolder();
			String alt = NormalizePath(AppendFileName(exeDir, candidate));
			if (auto ok = Try(alt); !IsNull(ok))
				return ok;
		}

		// 3) 最近一次加载脚本所在目录相对路径
		if (!IsNull(lastScriptFile)) {
			String baseDir = GetFileDirectory(lastScriptFile);
			if (!IsNull(baseDir)) {
				String alt = NormalizePath(AppendFileName(baseDir, candidate));
				if (auto ok = Try(alt); !IsNull(ok))
					return ok;
			}
		}

		// 4) 最后再对 candidate 做一次归一化尝试
		{
			String norm = NormalizePath(candidate);
			if (auto ok = Try(norm); !IsNull(ok))
				return ok;
		}

		return String(); // 未解析到有效文件
	};

};
