#include "../inc/ApiLibrary.h"
ApiLibrary::PublicApi* instance_ = nullptr;
static std::mutex g_api_singleton_mutex;



APILIB_API ApiLibrary::ApiInterfacePtr ApiLibrary::CreateApiInterface()
{
    std::lock_guard<std::mutex> lock(g_api_singleton_mutex);
    if (instance_ == nullptr)
        instance_ = new ApiLibrary::PublicApi();
    return static_cast<ApiLibrary::ApiInterfacePtr>(instance_);
}

APILIB_API void ApiLibrary::DestroyApiInterface()
{
    std::lock_guard<std::mutex> lock(g_api_singleton_mutex);
    if (!instance_)
        return;
    instance_->FreeUpResources();
    delete instance_;
    instance_ = nullptr;
}

APILIB_API ApiLibrary::ApiInterfacePtr ApiLibrary::CreateApiInterfaceInstance()
{
    return static_cast<ApiLibrary::ApiInterfacePtr>(new ApiLibrary::PublicApi());
}

APILIB_API void ApiLibrary::DestroyApiInterfaceInstance(ApiInterfacePtr api)
{
    if (!api)
        return;
    api->FreeUpResources();
    delete api;
}
static std::atomic<bool> g_log_inited{ false };
APILIB_API void ApiLibrary::InitUppLogging(const char* logfile, bool enableConsole)
{
	//Upp::MemoryBreakpoint(672);
    bool expected = false;
    if (!g_log_inited.compare_exchange_strong(expected, true)) {
        return; // 已初始化，避免重复
    }

    //SetDefaultCharset(CHARSET_UTF8);

#ifdef _WIN32
    SetLanguage(LNG_('Z', 'H', 'C', 'N'));
    SetDefaultCharset(CHARSET_UTF8);
#endif

    int flags = LOG_FILE;
    if (enableConsole)
        flags |= LOG_COUT;

    StdLogSetup(flags, logfile ? logfile : nullptr);

    LOG("NetLibrary logging initialized. console=" << enableConsole
        << ", file=" << (logfile ? logfile : "(default)"));
}