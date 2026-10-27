#pragma once

namespace ApiLibrary {

	class ScriptDispatcher {
		typedef ScriptDispatcher CLASSNAME;
	public:
		ScriptDispatcher() = default;
		~ScriptDispatcher();

		bool Init(const ScriptRuntimeConfig& cfg, Upp::String* err = nullptr);
		void Shutdown();

		void DispatchInvoke(const ScriptInvokeRequest& req);
		void DispatchCallback(const Upp::String& sid, const Upp::String& callback_ev, const AssetLib::BinaryData& payload);

		bool ReloadAll(const Upp::String& entry_file, Upp::String* err = nullptr);
		int GetWorkerCount() const;

	private:
		ScriptRuntimeConfig cfg_;
		std::vector<std::unique_ptr<ScriptRuntimeWorker>> workers_;

	private:
		int SelectWorkerIndex(const Upp::String& sid) const;
	};
}
