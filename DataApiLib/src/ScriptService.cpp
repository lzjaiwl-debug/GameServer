#include "../inc/ApiLibrary.h"

ApiLibrary::ScriptService::ScriptService()
	: dispatcher_(std::make_unique<ScriptDispatcher>()) {
}

ApiLibrary::ScriptService::~ScriptService()
{
	Stop();
}

bool ApiLibrary::ScriptService::Start(const ScriptRuntimeConfig& cfg, std::string* err)
{
	if (started_.load(std::memory_order_acquire))
		return true;

	cfg_ = cfg;
	if (!dispatcher_)
		dispatcher_ = std::make_unique<ScriptDispatcher>();

	if (!dispatcher_->Init(cfg_, err))
		return false;

	started_.store(true, std::memory_order_release);
	return true;
}

void ApiLibrary::ScriptService::Stop()
{
	bool expected = true;
	if (!started_.compare_exchange_strong(expected, false, std::memory_order_acq_rel))
		return;

	if (dispatcher_)
		dispatcher_->Shutdown();
}

void ApiLibrary::ScriptService::PostInvoke(const ScriptInvokeRequest& req)
{
	if (!started_.load(std::memory_order_acquire)) {
		LOG("[ScriptService] PostInvoke ignored: service not started");
		return;
	}
	if (dispatcher_)
		dispatcher_->DispatchInvoke(req);
}

void ApiLibrary::ScriptService::PostCallback(const std::string& sid, const std::string& callback_ev, const AssetLib::BinaryData& payload)
{
	if (!started_.load(std::memory_order_acquire)) {
		LOG("[ScriptService] PostCallback ignored: service not started");
		return;
	}
	if (dispatcher_)
		dispatcher_->DispatchCallback(sid, callback_ev, payload);
}

bool ApiLibrary::ScriptService::Reload(const std::string& entry_file, std::string* err)
{
	if (!started_.load(std::memory_order_acquire)) {
		if (err)
			*err = "脚本服务未启动";
		return false;
	}
	if (!dispatcher_) {
		if (err)
			*err = "脚本调度器未初始化";
		return false;
	}
	return dispatcher_->ReloadAll(entry_file, err);
}

int ApiLibrary::ScriptService::GetWorkerCount() const
{
	return dispatcher_ ? dispatcher_->GetWorkerCount() : 0;
}
