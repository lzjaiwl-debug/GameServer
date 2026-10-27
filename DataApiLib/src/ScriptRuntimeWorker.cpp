#include "../inc/ApiLibrary.h"

ApiLibrary::ScriptRuntimeWorker::ScriptRuntimeWorker(int worker_id)
	: worker_id_(worker_id) {
}

ApiLibrary::ScriptRuntimeWorker::~ScriptRuntimeWorker()
{
	Stop();
}

bool ApiLibrary::ScriptRuntimeWorker::Start(const ScriptRuntimeConfig& cfg, Upp::String* err)
{
	if (running_.load(std::memory_order_acquire))
		return true;

	cfg_ = cfg;
	if (!InitRuntime(err))
		return false;

	running_.store(true, std::memory_order_release);
	worker_.ThreadRun();
	return true;
}

void ApiLibrary::ScriptRuntimeWorker::Stop()
{
	bool expected = true;
	if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
		ReleaseRuntime();
		return;
	}

	worker_.Stop();

	ReleaseRuntime();
}

void ApiLibrary::ScriptRuntimeWorker::PostInvoke(const ScriptInvokeRequest& req)
{
	if (!running_.load(std::memory_order_acquire))
		return;

	Task task;
	task.type = TaskType::INVOKE;
	task.req = req;
	EnqueueTask(std::move(task));
}

void ApiLibrary::ScriptRuntimeWorker::PostCallback(const Upp::String& sid, const Upp::String& callback_ev, const AssetLib::BinaryData& payload)
{
	if (!running_.load(std::memory_order_acquire))
		return;

	Task task;
	task.type = TaskType::CALLBACK;
	task.sid = sid;
	task.callback_ev = callback_ev;
	task.payload = payload;
	EnqueueTask(std::move(task));
}

bool ApiLibrary::ScriptRuntimeWorker::Reload(const Upp::String& entry_file, Upp::String* err)
{
	if (!api_) {
		if (err) *err = "脚本运行时未初始化";
		return false;
	}

	try {
		api_->ResetScriptEnvironment();
		if (!Upp::IsNull(entry_file) && !entry_file.IsEmpty()) {
			api_->LoadScriptFile(~entry_file);
			cfg_.entry_file = entry_file;
		}
		return true;
	}
	catch (const Upp::Exc& ex) {
		if (err) *err = ex;
		LOG("[ScriptRuntimeWorker] Reload failed(worker=" << worker_id_ << "): " << ex);
		return false;
	}
	catch (const std::exception& ex) {
		if (err) *err = ex.what();
		LOG("[ScriptRuntimeWorker] Reload std::exception(worker=" << worker_id_ << "): " << ex.what());
		return false;
	}
}

bool ApiLibrary::ScriptRuntimeWorker::InitRuntime(Upp::String* err)
{
	if (api_)
		return true;

	api_ = ApiLibrary::CreateApiInterfaceInstance();
	if (!api_) {
		if (err) *err = "CreateApiInterfaceInstance 返回空指针";
		return false;
	}

	try {
		Upp::String script_root = cfg_.script_root;
		if (Upp::IsNull(script_root) || script_root.IsEmpty()) {
			if (err)
				*err = "ScriptRuntimeConfig.script_root 不能为空（必须由调用方配置）";
			ReleaseRuntime();
			return false;
		}

		api_->SetScriptPath(~script_root);
		
		for (const auto& [event_name, handler] : cfg_.custom_event_handlers) {
			api_->On(event_name, handler);
		}

		api_->On("apiexecute", [this](const AssetLib::EventArgs& args) {
			HandleScriptApiExecute(args);
		});

		if (!Upp::IsNull(cfg_.entry_file) && !cfg_.entry_file.IsEmpty())
			api_->LoadScriptFile(~cfg_.entry_file);

		return true;
	}
	catch (const Upp::Exc& ex) {
		if (err) *err = ex;
		LOG("[ScriptRuntimeWorker] Init failed(worker=" << worker_id_ << "): " << ex);
		ReleaseRuntime();
		return false;
	}
	catch (const std::exception& ex) {
		if (err) *err = ex.what();
		LOG("[ScriptRuntimeWorker] Init std::exception(worker=" << worker_id_ << "): " << ex.what());
		ReleaseRuntime();
		return false;
	}
}

void ApiLibrary::ScriptRuntimeWorker::ReleaseRuntime()
{
	if (!api_)
		return;

	ApiLibrary::DestroyApiInterfaceInstance(api_);
	api_ = nullptr;
}

void ApiLibrary::ScriptRuntimeWorker::EnqueueTask(Task&& task)
{
	AssetLib::Event<void, Task&> task_ev([this](Task& t) {
		if (t.type == TaskType::INVOKE)
			ExecuteInvoke(t.req);
		else
			ExecuteCallback(t.sid, t.callback_ev, t.payload);
	});

	worker_.SubmitTask(std::move(task_ev), std::move(task), true, true);
}

void ApiLibrary::ScriptRuntimeWorker::ExecuteInvoke(const ScriptInvokeRequest& req)
{
	if (!api_)
		return;
	if (Upp::IsNull(req.event_name) || req.event_name.IsEmpty())
		return;

	auto begin = std::chrono::steady_clock::now();
	try {
		api_->Emit(req.event_name.ToStd(), req.payload);
	}
	catch (const Upp::Exc& ex) {
		LOG("[ScriptRuntimeWorker] Invoke failed(worker=" << worker_id_ << ", ev=" << req.event_name << "): " << ex);
	}
	catch (const std::exception& ex) {
		LOG("[ScriptRuntimeWorker] Invoke std::exception(worker=" << worker_id_ << ", ev=" << req.event_name << "): " << ex.what());
	}

	int elapsed = (int)std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now() - begin).count();
	if (req.timeout_ms > 0 && elapsed > req.timeout_ms) {
		LOG("[ScriptRuntimeWorker] Invoke timeout(worker=" << worker_id_ << ", ev=" << req.event_name
			<< ", elapsed_ms=" << elapsed << ", budget_ms=" << req.timeout_ms << ")");
	}
}

void ApiLibrary::ScriptRuntimeWorker::ExecuteCallback(const Upp::String& sid, const Upp::String& callback_ev, const AssetLib::BinaryData& payload)
{
	if (!api_)
		return;
	if (Upp::IsNull(callback_ev) || callback_ev.IsEmpty())
		return;

	try {
		api_->Emit(callback_ev.ToStd(), payload);
	}
	catch (const Upp::Exc& ex) {
		LOG("[ScriptRuntimeWorker] Callback failed(worker=" << worker_id_ << ", sid=" << sid
			<< ", ev=" << callback_ev << "): " << ex);
	}
	catch (const std::exception& ex) {
		LOG("[ScriptRuntimeWorker] Callback std::exception(worker=" << worker_id_ << ", sid=" << sid
			<< ", ev=" << callback_ev << "): " << ex.what());
	}
}

void ApiLibrary::ScriptRuntimeWorker::HandleScriptApiExecute(const AssetLib::EventArgs& args)
{
	if (cfg_.on_api_execute)
		cfg_.on_api_execute(args);
}
