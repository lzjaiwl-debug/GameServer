#include "../inc/ApiLibrary.h"

ApiLibrary::ScriptDispatcher::~ScriptDispatcher()
{
	Shutdown();
}

bool ApiLibrary::ScriptDispatcher::Init(const ScriptRuntimeConfig& cfg, Upp::String* err)
{
	Shutdown();
	cfg_ = cfg;

	int worker_count = cfg_.worker_count > 0 ? cfg_.worker_count : 1;

	workers_.reserve((size_t)worker_count);
	for (int i = 0; i < worker_count; ++i) {
		auto worker = std::make_unique<ScriptRuntimeWorker>(i);
		Upp::String start_err;
		if (!worker->Start(cfg_, &start_err)) {
			if (err)
				*err = Upp::Format("启动脚本Worker[%d]失败: %s", i, ~start_err);
			Shutdown();
			return false;
		}
		workers_.push_back(std::move(worker));
	}
	return true;
}

void ApiLibrary::ScriptDispatcher::Shutdown()
{
	for (auto& worker : workers_) {
		if (worker)
			worker->Stop();
	}
	workers_.clear();
}

void ApiLibrary::ScriptDispatcher::DispatchInvoke(const ScriptInvokeRequest& req)
{
	if (workers_.empty()) {
		LOG("[ScriptDispatcher] DispatchInvoke ignored: no worker available");
		return;
	}

	int idx = SelectWorkerIndex(req.sid);
	if (idx < 0 || idx >= (int)workers_.size()) {
		LOG("[ScriptDispatcher] DispatchInvoke route error, sid=" << req.sid);
		return;
	}
	workers_[(size_t)idx]->PostInvoke(req);
}

void ApiLibrary::ScriptDispatcher::DispatchCallback(const Upp::String& sid, const Upp::String& callback_ev, const AssetLib::BinaryData& payload)
{
	if (workers_.empty()) {
		LOG("[ScriptDispatcher] DispatchCallback ignored: no worker available");
		return;
	}

	int idx = SelectWorkerIndex(sid);
	if (idx < 0 || idx >= (int)workers_.size()) {
		LOG("[ScriptDispatcher] DispatchCallback route error, sid=" << sid << ", callback=" << callback_ev);
		return;
	}
	workers_[(size_t)idx]->PostCallback(sid, callback_ev, payload);
}

bool ApiLibrary::ScriptDispatcher::ReloadAll(const Upp::String& entry_file, Upp::String* err)
{
	if (workers_.empty()) {
		if (err)
			*err = "脚本Worker未启动";
		return false;
	}

	for (const auto& worker : workers_) {
		if (!worker)
			continue;

		Upp::String one_err;
		if (!worker->Reload(entry_file, &one_err)) {
			if (err)
				*err = Upp::Format("重载脚本失败，worker=%d, err=%s", worker->GetWorkerId(), ~one_err);
			return false;
		}
	}
	return true;
}

int ApiLibrary::ScriptDispatcher::GetWorkerCount() const
{
	return (int)workers_.size();
}

int ApiLibrary::ScriptDispatcher::SelectWorkerIndex(const Upp::String& sid) const
{
	if (workers_.empty())
		return -1;
	if (Upp::IsNull(sid) || sid.IsEmpty())
		return 0;

	const std::string key = sid.ToStd();
	size_t hv = std::hash<std::string>{}(key);
	return (int)(hv % workers_.size());
}
