#include "../inc/ApiLibrary.h"
#include <random>

AssetLib::EventBus<> assetEventBus;
void ApiLibrary::PublicApi::LoadScriptFile(const char* scriptfile)
{
    script_dir_ = GetFileDirectory(scriptfile);
    file_.SetScriptDir(script_dir_);
    LoadScriptWithImports(scriptfile);
}

void ApiLibrary::PublicApi::CollectImportDeps(const String& file, Index<String>& deps, Index<String>& stack)
{
    String norm = NormalizePath(file);
    if(deps.Find(norm) >= 0)
        return;

    if(stack.Find(norm) >= 0) {
        String chain;
        for(int i = 0; i < stack.GetCount(); ++i)
            chain << (i ? " -> " : "") << stack[i];
        chain << " -> " << norm;
        throw Upp::Exc(String("Esc #import cycle detected: ") + chain);
    }

    if(!FileExists(norm))
        throw Upp::Exc(String("Esc #import file not found: ") + norm);

    stack.Add(norm);
    deps.Add(norm);

    String content = LoadFile(norm);
    if(content.IsVoid())
        throw Upp::Exc(String("Esc #import failed to read file: ") + norm);

    String baseDir = GetFileDirectory(norm);
    Vector<String> lines = Split(content, '\n', false);
    for(const String& rawLine : lines) {
        String importPath;
        if(TryParseImportLine(rawLine, importPath)) {
            String resolved = importPath;
            resolved.Replace("\\", "/");
            if(!IsFullPath(resolved))
                resolved = AppendFileName(baseDir, resolved);
            resolved = NormalizePath(resolved);
            CollectImportDeps(resolved, deps, stack);
        }
    }

    stack.Drop();
}
/// <summary>
/// 确定指定入口文件是否需要重新加载（根据收集到的依赖文件和上次加载时间判断）。
/// </summary>
/// <param name="entryFile">要检查的入口文件路径或标识。</param>
/// <param name="outDeps">输出参数，返回由 CollectImportDeps 收集到的依赖文件列表；该列表随后用于检查各依赖文件的修改时间。</param>
/// <returns>如果需要重新加载则返回 true（例如 last_loaded_stamp_ 为空或任一依赖文件的修改时间晚于 last_loaded_stamp_），否则返回 false。</returns>
bool ApiLibrary::PublicApi::NeedReloadForEntry(const String& entryFile, Index<String>& outDeps)
{
    Index<String> stack;
    CollectImportDeps(entryFile, outDeps, stack);

    if(IsNull(last_loaded_stamp_))
        return true;

    for(int i = 0; i < outDeps.GetCount(); ++i) {
        Time ft = GetFileTime(outDeps[i]);
        if(!IsNull(ft) && ft > last_loaded_stamp_)
            return true;
    }
    return false;
}

void ApiLibrary::PublicApi::ResetScriptEnvironment()
{
    // Create a fresh Esc environment: clear script-defined globals and namespaces.
    // Then re-register stdlib + natives.
    static_cast<ScriptObject*>(this)->Reset();

    LoadStdLib();
    mysqlServer_.RegisterNative(this);
    updata_.RegisterNative(this);
    //scheduledTask_.RegisterNative(this);
    file_.RegisterNative(this);
    this->RegisterNative("LoadScriptFile(...)", THISBACK(Initialize));
    this->RegisterNative("ToJson(...)", THISBACK(ToJson));
    this->RegisterNative("JsonToValue(...)", THISBACK(JsonToValue));
    this->RegisterNative("PostEmit(...)", THISBACK(PostEmit));
    this->RegisterNative("SetGlobalVar(...)", THISBACK(SetGlobalVar));
    this->RegisterNative("GetGlobalVar(...)", THISBACK(GetGlobalVar));
    this->RegisterNative("GetScriptDir(...)", THISBACK(GetScriptDir));
    this->RegisterNative("TokenIssue(...)", THISBACK(TokenIssue));
    this->RegisterNative("TokenValidate(...)", THISBACK(TokenValidate));
    this->RegisterNative("TokenRevoke(...)", THISBACK(TokenRevoke));
    this->RegisterNative("GetTokenInfo(...)", THISBACK(GetTokenInfo));
    this->RegisterNative("GetTimeUtc(...)", THISBACK(GetTimeUtc));
    this->RegisterNative("GetDateUtc(...)", THISBACK(GetDateUtc));
    this->RegisterNative("GetMonthUtc(...)", THISBACK(GetMonthUtc));
    this->RegisterNative("GetYearUtc(...)", THISBACK(GetYearUtc));
}

nlohmann::json ApiLibrary::PublicApi::GetScriptFunctions(const char* scriptns)
{
    nlohmann::json result = nlohmann::json::array();
    String ns(scriptns);
    if(this->GetNamespace(ns)) {
        Vector<String> nsval = this->GetNamespaceFunctions(ns);
        for(auto& name: nsval) {
            result.push_back(name.ToStd());
        }
    }
	return result;
}
    
void ApiLibrary::PublicApi::Emit(std::string ev, AssetLib::BinaryData data)
{
	Vector<String> ns = Split(ev.c_str(), "::");
    if(!this->GetNamespace(ns[0]))
		throw CParser::Error(Format("Emit failed: unknown namespace '%s'", ns[0]));
    
    if (data) {
        EscValue args;
        Value val = ParseJSON(data);
        String path = val["api"][~val["apiname"]];
        if (!val.IsError() && !val.IsNull())
            ParseValue(val, args);
        this->CallVar(ev, INT_MAX, args);
    }
    this->CallVar(ev, INT_MAX);
}

void ApiLibrary::PublicApi::On(std::string ev, AssetLib::EventHandler&& fun)
{
    assetEventBus.On(ev, std::move(fun));
}

void ApiLibrary::PublicApi::RegExtFun(std::string ns, std::string ev, AssetLib::EventHandlerR&& fun)
{
    auto extev = [=](const Vector<EscValue>& args)-> EscValue {
        AssetLib::EventArgs evargs;
        AnyListFromEscValueList(args, evargs);
        return EscValueFromAny(fun(evargs));
    };
    this->RegisterNativeFn(ev.c_str(), extev, ns.c_str());
}

void ApiLibrary::PublicApi::RegExtFun(std::string ns, std::string ev, AssetLib::EventHandler&& fun)
{
	auto extev = [=](EscEscape& args) {
        AssetLib::EventArgs evargs;
		Vector<EscValue> vecArgs;
        for(auto& arg : args.arg)
			vecArgs.Add(arg);
        AnyListFromEscValueList(vecArgs, evargs);
        fun(evargs);
    };
	Upp::CallbackN<EscEscape&> cb(extev, 1);
	this->RegisterNative(ev.c_str(), cb, ns.c_str());
}

void ApiLibrary::PublicApi::UploadData(const char* id, AssetLib::BinaryData data)
{
}

void ApiLibrary::PublicApi::GetScriptDir(EscEscape& e)
{
	e = script_dir_;
}

void ApiLibrary::PublicApi::SetGlobalVar(EscEscape& e)
{
    if(e.GetCount() < 2)
        throw CParser::Error("SetGlobalVar requires at least 2 arguments: name and value");
    String name = e[0];
    EscValue value = e[1];
    {
        globalvars_.GetPut(name) = value; // 更新全局变量映射
	}
}

void ApiLibrary::PublicApi::GetGlobalVar(EscEscape& e)
{
    if(e.GetCount() < 1)
        throw CParser::Error("GetGlobalVar requires at least 1 argument: name");
    String name = e[0];
    EscValue value;
    {
        if(globalvars_.Find(name))
            value = globalvars_.Get(name);
        else
            value = EscValue();
    }
	e = value; // 返回变量值
}

void ApiLibrary::PublicApi::PostEmit(EscEscape& e)
{
    if(e.GetCount() < 2)
        throw CParser::Error("Emit requires at least 2 arguments: event name and data");
    String ev = e[0];
    String args = ValueToJson(e[1]);
    assetEventBus.Emit(ev.ToStd(), args.ToStd());
}

void ApiLibrary::PublicApi::FormatString(EscEscape& e)
{
    String fmt = e[0];
    String result;
    int argIndex = 1;
    for(int i = 0; i < fmt.GetLength(); ++i) {
        if(fmt[i] == '%' && i + 1 < fmt.GetLength()) {
            char spec = fmt[++i];
            if(spec == '%') {
                result.Cat('%');
            }
            else if(argIndex < e.GetCount()) {
                String argStr;
                switch(spec) {
                    case 's': argStr = (String)e[argIndex]; break;
                    case 'd': argStr = Format("%d", (int)e[argIndex].GetInt64()); break;
                    case 'f': argStr = Format("%f", e[argIndex].GetNumber()); break;
                    default: argStr = Format("%%%c", spec); break; // 未知格式原样输出
                }
                result.Cat(argStr);
                ++argIndex;
            }
            else {
                result << '%' << spec; // 参数不足时原样输出
            }
        }
        else {
            result.Cat(fmt[i]);
        }
    }
	e = result; // 返回格式化结果
}

// 生成安全随机令牌（十六进制）
String ApiLibrary::PublicApi::MakeToken(int bytes)
{
    String tok;
    tok.Reserve(bytes * 2);
    static std::random_device rd;
    static std::mt19937 gen(rd());
    static std::uniform_int_distribution<> dis(0, 255);
    for(int i = 0; i < bytes; ++i) {
        int b = dis(gen);
        tok.Cat(Format("%02x", b));
    }
    return tok;
}

// 颁发令牌：TokenIssue(userId, ttlSec=3600) -> string token
void ApiLibrary::PublicApi::TokenIssue(EscEscape& e)
{
    if(e.GetCount() < 1)
        e.ThrowError("TokenIssue requires at least 1 argument: userId");
    if (e[0].IsMap())
    {
        int ttl = e.GetCount() >= 2 ? e[1].GetInt() : 3600;
        if (ttl <= 0) ttl = 3600;
        Time now = GetSysTime();
        int64 exp = GetUTCSeconds(now) + ttl;

        String token = MakeToken(32);
        EscValue meta;
		meta.SetEmptyMap();
        meta.MapSet("user", e[0]);
        meta.MapSet("exp", exp);
        {
            std::lock_guard<std::mutex> lk(token_mutex_);
            token_store_.GetPut(token) = meta;
        }
        e = token;
		return;
    }
    e.ThrowError("");
}

// 验证令牌：TokenValidate(token) -> bool
void ApiLibrary::PublicApi::TokenValidate(EscEscape& e)
{
    if(e.GetCount() < 1)
        e.ThrowError("TokenValidate requires 1 argument: token");
    String token = e[0]; // 第一个参数

    EscValue meta;
    bool ok = false;

    {
        std::lock_guard<std::mutex> lk(token_mutex_);
        int pos = token_store_.Find(token);
        if(pos >= 0) {
            meta = token_store_.Get(token);
        }
    }

    if(!meta.IsMap()) {
        e = false;
        return;
    }

    EscValue expv = meta.MapGet("exp");
    if(!expv.IsInt64() && !expv.IsNumber()) {
        e = false;
        return;
    }

    int64 exp = expv.IsInt64() ? expv.GetInt64() : (int64)expv.GetNumber();
    int64 now = GetUTCSeconds(GetSysTime());
    ok = now <= exp;

    e = ok;
}

// 注销令牌：TokenRevoke(token) -> bool
void ApiLibrary::PublicApi::TokenRevoke(EscEscape& e)
{
    if(e.GetCount() < 1)
        e.ThrowError("TokenRevoke requires 1 argument: token");
    String token = e[0]; // 第一个参数
    bool removed = false;
    {
        std::lock_guard<std::mutex> lk(token_mutex_);
        int pos = token_store_.Find(token);
        if(pos >= 0) {
            token_store_.Remove(pos);
            removed = true;
        }
    }
    e = removed;
}

void ApiLibrary::PublicApi::GetTokenInfo(EscEscape& e)
{
    if (e.GetCount() < 1)
        e.ThrowError("TokenRevoke requires 1 argument: token");
	String token = e[0]; // 第一个参数
    {
        std::lock_guard<std::mutex> lk(token_mutex_);
        int pos = token_store_.Find(token);
        if (pos >= 0) {
            e = token_store_.Get(token).ArrayGet(0);
        }
        else
			e = EscValue(); // 返回 null
    }
}

void ApiLibrary::PublicApi::Initialize(EscEscape& e)
{
    // 支持绝对/相对路径，按多来源查找
    if(e.GetCount() < 1)
        e.ThrowError("Initialize requires at least 1 argument");

    auto LoadOne = [&](const String& msg) {
        // 先解析为可用文件路径
        String resolved = ResolveScriptPath(msg, Nuller());
        if(!IsNull(resolved) && FileExists(~resolved)) {
            LoadScript(resolved);
            LOG("PublicApi initialized by loading script file: " << resolved);
            return;
        }
        // 否则当作脚本文本处理
        LoadScriptFromString(msg);
        LOG("PublicApi initialized by loading script from string.");
    };

    if(e.GetCount() == 1) {
        String msg = e[0]; // 修正索引
        LoadOne(msg);
        LOG("PublicApi initialized with message: " << msg);
    }
    else {
        for(int i = 0; i < e.GetCount(); ++i) {
            String msg = e[i];
            LoadOne(msg);
        }
    }
}

bool ApiLibrary::PublicApi::TryParseImportLine(const String& line, String& outPath)
{
    String s = line;
    s = TrimLeft(s);
    if(!s.StartsWith("#import"))
        return false;

    s = s.Mid((int)strlen("#import"));
    s = TrimLeft(s);
    if(s.IsEmpty())
        return false;

    if(s[0] == '"' || s[0] == '\'') {
        int end = s.Find(s[0], 1);
        if(end <= 1)
            return false;
        outPath = s.Mid(1, end - 1);
        return !outPath.IsEmpty();
    }

    int end = 0;
    while(end < s.GetCount() && !IsSpace(s[end]))
        end++;
    outPath = s.Left(end);
    return !outPath.IsEmpty();
}

String ApiLibrary::PublicApi::ExpandScriptImports(const String& file, Index<String>& loaded, Index<String>& stack)
{
    String norm = NormalizePath(file);
    if(loaded.Find(norm) >= 0)
        return String();

    if(stack.Find(norm) >= 0) {
        String chain;
        for(int i = 0; i < stack.GetCount(); ++i)
            chain << (i ? " -> " : "") << stack[i];
        chain << " -> " << norm;
        throw Upp::Exc(String("Esc #import cycle detected: ") + chain);
    }

    if(!FileExists(norm))
        throw Upp::Exc(String("Esc #import file not found: ") + norm);

    stack.Add(norm);

    String content = LoadFile(norm);
    if(content.IsVoid())
        throw Upp::Exc(String("Esc #import failed to read file: ") + norm);

    String baseDir = GetFileDirectory(norm);
    StringBuffer out;
    out << "// --- begin file: " << norm << " ---\n";

    Vector<String> lines = Split(content, '\n', false);
    for(const String& rawLine : lines) {
        String importPath;
        if(TryParseImportLine(rawLine, importPath)) {
            String resolved = importPath;
            resolved.Replace("\\", "/");

            if(!IsFullPath(resolved))
                resolved = AppendFileName(baseDir, resolved);
            resolved = NormalizePath(resolved);

            out << ExpandScriptImports(resolved, loaded, stack);
            continue;
        }
        out << rawLine << "\n";
    }

    out << "// --- end file: " << norm << " ---\n";

    loaded.Add(norm);
    stack.Drop();
    return out;
}

void ApiLibrary::PublicApi::LoadScriptWithImports(const String& entryFile)
{
    Index<String> loaded;
    Index<String> stack;

    String resolved = ResolveScriptPath(entryFile, script_dir_);
    if(IsNull(resolved))
		throw Upp::Exc(String("Failed to resolve script path: ") + entryFile);

    Index<String> deps;
    if(NeedReloadForEntry(resolved, deps)) {
        ResetScriptEnvironment();
    }

    String expanded = ExpandScriptImports(resolved, loaded, stack);
    if(expanded.IsEmpty())
        throw Upp::Exc(String("Esc script is empty after import expansion: ") + resolved);
    String ns = GetFileTitle(entryFile);
    LoadScriptFromString(expanded, GetFileName(entryFile), ns);

    last_loaded_stamp_ = GetSysTime();
    last_loaded_deps_.Clear();
    for(int i = 0; i < deps.GetCount(); ++i)
        last_loaded_deps_.Add(deps[i]);
}

void ApiLibrary::PublicApi::ToJson(EscEscape& e)
{
    if(e.GetCount() < 1)
        throw CParser::Error("ToJson requires at least 1 argument");
    Value json;
    ParseEscValue(e[0], json);
    e = AsJSON(json, false);
}

String ApiLibrary::PublicApi::ValueToJson(const EscValue& v)
{
    Value json;
    ParseEscValue(v, json);
	return AsJSON(json, false);
}

void ApiLibrary::PublicApi::JsonToValue(EscEscape& e)
{
    if(e.GetCount() < 1)
        throw CParser::Error("JsonToValue requires at least 1 argument");
    String jsonStr = e[0];
    Value json = ParseJSON(jsonStr);
    EscValue ev;
    
    ParseValue(json, ev);
    e = ev;
}

void ApiLibrary::PublicApi::GetTimeUtc(EscEscape& e)
{
    if (e.GetCount() == 2)
    {
        try {
            String fmt = e[0];
			String s = e[1];
			Time now = ScanTime(~fmt, ~s);
            e = GetUTCSeconds(now);
            return;
        }
        catch (const std::exception& ex) {
            e.ThrowError(Format("Invalid time format: %s", ex.what()));
            return;
		}
    }
    Time now = GetSysTime();
    int64 utcSec = GetUTCSeconds(now);
	e = utcSec;
}

void ApiLibrary::PublicApi::TimeToDateUtc(EscEscape& e)
{
    if (e.GetCount() == 2)
    {
        try {
            String fmt = e[0];
            String tz = e[1];
			Date d = ScanDate(~fmt, ~tz);
			e = GetUTCSeconds(ToTime(d));
            return;
        }
        catch (const std::exception& ex) {
            e.ThrowError(Format("Invalid time value: %s", ex.what()));
            return;
        }
    }
    if(e.GetCount() < 1 || !e[0].IsInt64())
		e.ThrowError("TimeToDateUtc requires 1 argument: time in UTC seconds (int64)");
    Time now = TimeFromUTC(e[0].GetInt64());
    Date d = ToTime(now);
	e = GetUTCSeconds(ToTime(d));
}

void ApiLibrary::PublicApi::GetDateUtc(EscEscape& e)
{
    if (e.GetCount() == 2)
    {
        try {
            String fmt = e[0];
            String tz = e[1];
			Date now = ScanDate(~fmt, ~tz);
            e = GetUTCSeconds(ToTime(now));
            return;
        }
        catch (const std::exception& ex) {
            e.ThrowError(Format("Invalid time format: %s", ex.what()));
            return;
        }
    }
    Date now = GetSysDate();
    int64 utcDate = GetUTCSeconds(ToTime(now));
	e = utcDate;
}

void ApiLibrary::PublicApi::GetMonthUtc(EscEscape& e)
{
    if (e.GetCount() == 2)
    {
        try {
            String fmt = e[0];
            String tz = e[1];
			// Parse year-month, then normalize to first day of month
			Date now = ScanDate(~fmt, ~tz);
			if (IsNull(now)) {
				e = (int64)-1;
				return;
			}
			e = GetUTCSeconds(ToTime(Date(now.year, now.month, 1)));
            return;
        }
        catch (const std::exception& ex) {
            e.ThrowError(Format("Invalid time format: %s", ex.what()));
            return;
        }
    }
    if (e.GetCount() == 1 && e[0].IsInt64())
    {
        Time t = TimeFromUTC(e[0].GetInt64());
        Date d = ToTime(t);
        e = GetUTCSeconds(ToTime(Date(d.year, d.month, 1)));
		return;
    }
    Date now = GetSysDate();
    int64 utcMonth = GetUTCSeconds(ToTime(Date(now.year, now.month, 1)));
	e = utcMonth;
}

void ApiLibrary::PublicApi::GetYearUtc(EscEscape& e)
{
    if (e.GetCount() == 2)
    {
        try {
            String fmt = e[0];
            String tz = e[1];
			Date now = ScanDate(~fmt, ~tz);
			if (IsNull(now)) {
				e = (int64)-1;
				return;
			}
			e = GetUTCSeconds(ToTime(Date(now.year, 1, 1)));
            return;
        }
        catch (const std::exception& ex) {
            e.ThrowError(Format("Invalid time format: %s", ex.what()));
            return;
        }
    }
    if (e.GetCount() == 1 && e[0].IsInt64())
    {
        Time t = TimeFromUTC(e[0].GetInt64());
        Date d = ToTime(t);
		e = GetUTCSeconds(ToTime(Date(d.year, 1, 1)));
    }
    Date now = GetSysDate();
    int64 utcYear = GetUTCSeconds(ToTime(Date(now.year, 1, 1)));
	e = utcYear;
}
