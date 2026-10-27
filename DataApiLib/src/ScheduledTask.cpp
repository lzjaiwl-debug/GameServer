#include "../inc/ApiLibrary.h"

void ApiLibrary::ScheduledTask::ScheduleTask(String fnName, Value val)
{
	if(!scriptObj_)
		return;
	ScriptObject* so = scriptObj_;
	String capturedFn = fnName;
	if(!val.IsNull() && val.GetCount() > 0) {
		// 如果有参数，构造一个 lambda 包装原函数和参数
		Vector<EscValue> args;
		for(auto& v : val)
		{
			if(v.Is<String>())
				args.Add(v.Get<String>());
			else if(v.Is<int>())
				args.Add(v.Get<int>());
			else if(v.Is<int64_t>())
				args.Add(v.Get<int64_t>());
			else if(v.Is<double>())
				args.Add(v.Get<double>());
		}
		so->Call(capturedFn, args, INT_MAX);
		return;
	}
	so->CallVar(capturedFn, INT_MAX);
}

// AddScheduledTask(functionName, cycleTicks)
// Esc usage: var id = AddScheduledTask("myTimerFunc", 5);
// myTimerFunc will be called every 5 * 1000ms = 5 seconds
void ApiLibrary::ScheduledTask::AddScheduledTask(EscEscape& e)
{
	if (e.GetCount() < 2) {
		e.ThrowError("AddScheduledTask requires 2 arguments: function name and cycle count");
		return;
	}
	if (!scriptObj_) {
		e.ThrowError("ScheduledTask not initialized (no ScriptObject)");
		return;
	}

	String fnName = e[0];
	int cycle = e[1].GetInt();
	if (cycle <= 0) {
		e.ThrowError("Cycle must be > 0");
		return;
	}
	Value val;
	if (e.GetCount() > 2 && !e[2].IsVoid() && e[2].IsArray()) {
		for(auto& v : e[2].GetArray())
		{
			if(v.IsArray())
				val.Add((String)v);
			else if(v.IsNumber())
				val.Add(v.GetNumber());
			else
				val = Nuller();
		}
	}
	else
		val = Nuller();
	if (!timeEvent_.IsRun())
		timeEvent_.TimeRun(1000);
	timeEvent_.CreateTask({ this,&ApiLibrary::ScheduledTask::ScheduleTask }, cycle, fnName, val);
}

// RemoveScheduledTask(taskId)
// Esc usage: RemoveScheduledTask(id);
void ApiLibrary::ScheduledTask::RemoveScheduledTask(EscEscape& e)
{
	if (e.GetCount() < 1) {
		e.ThrowError("RemoveScheduledTask requires 1 argument: task ID");
		return;
	}
	String fnName = e[0];
	timeEvent_.RemoveTaskIf([this, &fnName](const std::tuple<String, Value>& payload) {
		const String& name = std::get<0>(payload);
		return name == fnName;
	});
}
