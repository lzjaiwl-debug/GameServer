#pragma once

namespace ApiLibrary {

	class ScriptRuntimeWorker {
		typedef ScriptRuntimeWorker CLASSNAME;
	public:
		explicit ScriptRuntimeWorker(int worker_id);
		~ScriptRuntimeWorker();

		bool Start(const ScriptRuntimeConfig& cfg, Upp::String* err = nullptr);
		void Stop();

		void PostInvoke(const ScriptInvokeRequest& req);
		void PostCallback(const Upp::String& sid, const Upp::String& callback_ev, const AssetLib::BinaryData& payload);

		bool Reload(const Upp::String& entry_file, Upp::String* err = nullptr);
		int GetWorkerId() const { return worker_id_; }

	private:
		enum class TaskType {
			INVOKE,
			CALLBACK,
		};

		struct Task {
			TaskType type = TaskType::INVOKE;
			ScriptInvokeRequest req;
			Upp::String sid;
			Upp::String callback_ev;
			AssetLib::BinaryData payload;
		};

		int worker_id_ = -1;
		std::atomic<bool> running_{ false };
		ScriptRuntimeConfig cfg_;
		ApiInterface* api_ = nullptr;
		AssetLib::ThreadWork<void, Task> worker_;

	private:
		bool InitRuntime(Upp::String* err);
		void ReleaseRuntime();
		void EnqueueTask(Task&& task);
		void ExecuteInvoke(const ScriptInvokeRequest& req);
		void ExecuteCallback(const Upp::String& sid, const Upp::String& callback_ev, const AssetLib::BinaryData& payload);
		void HandleScriptApiExecute(const AssetLib::EventArgs& args);
	};
}
