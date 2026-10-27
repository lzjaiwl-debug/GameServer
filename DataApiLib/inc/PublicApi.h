#pragma once
namespace ApiLibrary {
	class PublicApi : public ScriptObject, public ApiInterface {
		typedef PublicApi CLASSNAME;
	public:
		PublicApi() {
			// 可选：加载标准库与扫描脚本到全局
			LoadStdLib();
			mysqlServer_.RegisterNative(this);
			updata_.RegisterNative(this);
			//scheduledTask_.RegisterNative(this);
			file_.RegisterNative(this);
			this->RegisterNative("LoadScriptFile(...)", THISBACK(Initialize));
			this->RegisterNative("ToJson(...)", THISBACK(ToJson));
			this->RegisterNative("JsonToValue(...)", THISBACK(JsonToValue));
			this->RegisterNative("PostEmit(...)", THISBACK(PostEmit));
			this->RegisterNative("FormatString(...)", THISBACK(FormatString));
			this->RegisterNative("SetGlobalVar(...)", THISBACK(SetGlobalVar));
			this->RegisterNative("GetGlobalVar(...)", THISBACK(GetGlobalVar));
			this->RegisterNative("GetScriptDir(...)", THISBACK(GetScriptDir));
			// Token APIs
			this->RegisterNative("TokenIssue(...)", THISBACK(TokenIssue));
			this->RegisterNative("TokenValidate(...)", THISBACK(TokenValidate));
			this->RegisterNative("TokenRevoke(...)", THISBACK(TokenRevoke));
			this->RegisterNative("GetTokenInfo(...)", THISBACK(GetTokenInfo));
			this->RegisterNative("GetTimeUtc(...)", THISBACK(GetTimeUtc));
			this->RegisterNative("GetDateUtc(...)", THISBACK(GetDateUtc));
			this->RegisterNative("GetMonthUtc(...)", THISBACK(GetMonthUtc));
			this->RegisterNative("GetYearUtc(...)", THISBACK(GetYearUtc));
			// ScanToGlobals(...); // 可选：扫描内嵌脚本
		}
		/// <summary>
		/// 入口脚本加载。
		/// </summary>
		/// <param name="scriptfile">要加载的脚本文件的路径。</param>
		virtual void LoadScriptFile(const char* scriptfile) override;
		virtual ~PublicApi() {
		}
		virtual void FreeUpResources() override {
			//scheduledTask_.Stop();
		}
		virtual void Emit(std::string ev, AssetLib::BinaryData data) override;
		virtual void On(std::string ev, AssetLib::EventHandler&& fun) override;
		virtual void RegExtFun(std::string ns, std::string ev, AssetLib::EventHandlerR&& fun) override;
		virtual void RegExtFun(std::string ns, std::string ev, AssetLib::EventHandler&& fun) override;
		virtual void UploadData(const char* id, AssetLib::BinaryData data) override;
		virtual void SetScriptPath(const char* path) override {
			script_dir_ = path;
			file_.SetScriptDir(path);
		}
		virtual void ResetScriptEnvironment() override;
		virtual nlohmann::json GetScriptFunctions(const char* scriptns) override;
	private:
		MysqlServer mysqlServer_;
		updata updata_;
		File file_;
		//ScheduledTask scheduledTask_;
		VectorMap<String, EscValue> globalvars_; // 全局变量映射，供脚本访问
		String script_dir_; // 脚本目录，用于相对路径加载
		// 简易令牌存储（内存），键：token，值：{ "user": String, "exp": int64(UTC秒) }
		VectorMap<String, EscValue> token_store_;
		std::mutex token_mutex_;
		void GetScriptDir(EscEscape& e);
		void SetGlobalVar(EscEscape& e);
		void GetGlobalVar(EscEscape& e);
		void PostEmit(EscEscape& e);
		void FormatString(EscEscape& e);
		/// <summary>
		/// 初始化脚本对象，由初始脚本将所有脚本注册进来。
		/// </summary>
		/// <param name="e">要初始化的 EscEscape 对象的引用。</param>
		void Initialize(EscEscape& e);
		/// <summary>
		/// 将对象序列化为 JSON，并将结果写入提供的 EscEscape 对象。
		/// </summary>
		/// <param name="e">用于写入或转义 JSON 输出的 EscEscape 对象的引用。</param>
		void ToJson(EscEscape& e);
		String ValueToJson(const EscValue& v);
		void JsonToValue(EscEscape& e);
		void GetTimeUtc(EscEscape& e);
		void GetDateUtc(EscEscape& e);
		void GetMonthUtc(EscEscape& e);
		void GetYearUtc(EscEscape& e);
		void TimeToDateUtc(EscEscape& e);
		// Token 原生方法
		void TokenIssue(EscEscape& e);
		void TokenValidate(EscEscape& e);
		void TokenRevoke(EscEscape& e);
		void GetTokenInfo(EscEscape& e);
		// 生成安全随机令牌
		static String MakeToken(int bytes = 32);
		void LoadScriptWithImports(const String& entryFile);
		String ExpandScriptImports(const String& file, Index<String>& loaded, Index<String>& stack);
		bool TryParseImportLine(const String& line, String& outPath);
		void CollectImportDeps(const String& file, Index<String>& deps, Index<String>& stack);
		bool NeedReloadForEntry(const String& entryFile, Index<String>& outDeps);
		Vector<String> last_loaded_deps_;
		Time last_loaded_stamp_;
	};
};