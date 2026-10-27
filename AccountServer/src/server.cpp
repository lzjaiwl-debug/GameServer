#include "../AccountServer.h"
#ifdef _WIN32
#pragma comment(lib, "Ws2_32.lib")
#endif

// 定义 HttpServer 静态成员
std::atomic_bool AccountServer::server::g_running{ false };
std::condition_variable AccountServer::server::g_cv;

AccountServer::server::server()
{
	assetEventBus.Once("global_set", LoadGlobalConfig);
}

void AccountServer::server::Start() {
	SetupSignalHandlers();
	g_running.store(true, std::memory_order_relaxed);
#ifdef _WIN32
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);
#endif
	String configPath = GetExeFolder() << "\\AccountServer.config";
	if (!FileExists(configPath))
	{
		LOG("Configuration file not found: " << configPath);
		return;
	}

	try {
		std::string configJson;
		std::string localHardwareCode;
		std::string localHardwareSerial;
		std::string licenseErr;
		if (!AssetLib::ServerConfigLicense::LoadAndUnsealLocalConfigFile(configPath.ToStd(), configJson,
			&localHardwareSerial, &localHardwareCode, &licenseErr)) {
			LOG("Configuration authorization failed: " << licenseErr);
			return;
		}
		config = NetLibrary::njson::parse(configJson, nullptr, true, false, true);
		Start_Mysql();
		if (!mysql.IsConnected())
			return;
		assetEventBus.Emit("mysql_query_ret", "global_set", "SELECT cfg_key, cfg_value FROM global_config WHERE zone_scope IS NULL", WithDeepCopy< Vector<Value>>(Vector<Value>{}));
		timeAsync.TimeRun(10);
		if (config.contains("adminlisten") && config["adminlisten"].is_object())
		{
			adminlisten_handler.SetTimeAsync(&timeAsync);
			adminlisten_handler.StartAdminListen(config["adminlisten"]);
		}
	}
	catch (const NetLibrary::njson::parse_error& e) {
		LOG("JSON parse error in configuration file: " << e.what());
		return;
	}
	catch (const NetLibrary::njson::type_error& e) {
		LOG("JSON type error in configuration file: " << e.what());
		return;
	}
	catch (const NetLibrary::njson::exception& e) {
		LOG("JSON error in configuration file: " << e.what());
		return;
	}
	catch (const Upp::Exc& ex) {
		LOG("Error initializing AccountServer server: " << ex);
		return;
	}
	catch (const std::exception& ex) {
		LOG("Standard exception initializing AccountServer server: " << ex.what());
		return;
	}

	WaitForShutdown();
}

void AccountServer::server::Stop()
{
	adminlisten_handler.StopAdminListen();
	timeAsync.Stop();

	if (mysql.IsConnected())
		mysql.Disconnect();
	HandleSignal(0); // 触发条件变量以退出 WaitForShutdown
}

void AccountServer::server::Start_Mysql()
{
	if(!sharedFeatures::IsValidMysqlconf(config["mysqlconf"]))
	{
		LOG("Configuration file missing or invalid 'mysqlconf' section.");
		return;
	}
	auto mysqlconf = config["mysqlconf"];
	if (!mysql.ConnectSql(mysqlconf["host"].get<std::string>().c_str(), mysqlconf["port"].get<int>(), mysqlconf["user"].get<std::string>().c_str(), mysqlconf["password"].get<std::string>().c_str(), mysqlconf["dbname"].get<std::string>().c_str())) {
		LOG("Failed to connect to MySQL: " << mysql.GetLastError());
		return;
	}
}

void AccountServer::server::StartCreateAdminWizardAsync()
{
	adminlisten_handler.StartCreateAdminWizardAsync();
}


