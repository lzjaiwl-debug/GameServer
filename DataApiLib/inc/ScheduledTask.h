#pragma once
namespace ApiLibrary {
	class ScheduledTask {
		typedef ScheduledTask CLASSNAME;
	public:
		ScheduledTask() : scriptObj_(nullptr) {
			timeEvent_.TimeRun(1000); // 每秒检查一次任务
		}
		~ScheduledTask() { 
			timeEvent_.Stop(); 
			scriptObj_ = nullptr; 
		}

		void RegisterNative(ScriptObject* so) {
			scriptObj_ = so;
			so->RegisterNative("AddScheduledTask(...)", THISBACK(AddScheduledTask), "ScheduledTask");
			so->RegisterNative("RemoveScheduledTask(...)", THISBACK(RemoveScheduledTask), "ScheduledTask");
		}
		void Stop() { timeEvent_.Stop(); }
	private:
		ScriptObject* scriptObj_;
		AssetLib::TimeEventAsync<void, String, Value> timeEvent_;
		void ScheduleTask(String fnName, Value val);
		void AddScheduledTask(EscEscape& e);
		void RemoveScheduledTask(EscEscape& e);
	};
}