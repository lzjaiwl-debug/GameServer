#include <atomic>
#include <condition_variable>
#include <csignal>
#include <mutex>
#include <vector>
#include "AccountServer.h"

#ifdef _WIN32
#include <windows.h>
static std::string WideToUtf8(const std::wstring& w)
{
    if (w.empty()) return {};
    int len = ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
        nullptr, 0, nullptr, nullptr);
    std::string s(len, '\0');
    ::WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
        s.data(), len, nullptr, nullptr);
    return s;
}
#endif


CONSOLE_APP_MAIN
{
    NetLibrary::InitUppLogging(GetExeFolder() << "\\NetLibrary.log", true);
    ApiLibrary::InitUppLogging(GetExeFolder() << "\\ApiLibrary.log", true);
    StdLogSetup(LOG_COUT | LOG_FILE);
    AccountServer::server server;
    std::atomic<bool> running{ true };
    std::atomic<bool> create_admin_exclusive{ false };
    AccountServer::commandTool cmdtool;
    cmdtool.RegisterCommand("help", "显示命令帮助", [&cmdtool](const Vector<String>&) {
        cmdtool.PrintHelp();
    }, false);
    cmdtool.RegisterCommand("create-admin", "进入交互式超管创建向导（独占输入模式）", [&server, &create_admin_exclusive](const Vector<String>&) {
        if (create_admin_exclusive.exchange(true, std::memory_order_acq_rel)) {
            Cout() << "create-admin 已在执行中，请先完成当前向导\n";
            return;
        }

        server.StartCreateAdminWizardAsync();
        create_admin_exclusive.store(false, std::memory_order_release);
    }, false);
    cmdtool.RegisterCommand("q", "停止服务并退出", [&server, &running](const Vector<String>&) {
        if (running.exchange(false, std::memory_order_relaxed))
            server.Stop();
    }, false);
    cmdtool.RegisterCommand("exit", "停止服务并退出", [&server, &running](const Vector<String>&) {
        if (running.exchange(false, std::memory_order_relaxed))
            server.Stop();
    }, false);

    std::thread server_thread([&server, &running] {
        server.Start();
        running.store(false, std::memory_order_relaxed);
    });

#ifdef _WIN32
    HANDLE hin = GetStdHandle(STD_INPUT_HANDLE);
    if (hin != INVALID_HANDLE_VALUE) {
        const DWORD MAX_PEEK = 64;
        INPUT_RECORD recs[MAX_PEEK];
        while (running.load(std::memory_order_relaxed)) {
            if (create_admin_exclusive.load(std::memory_order_acquire)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            DWORD events = 0;
            if (!GetNumberOfConsoleInputEvents(hin, &events)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            if (events == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            DWORD toPeek = events > MAX_PEEK ? MAX_PEEK : events;
            DWORD read = 0;
            if (!PeekConsoleInputA(hin, recs, toPeek, &read) || read == 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            bool hasPrintableKey = false;
            for (DWORD i = 0; i < read; ++i) {
                if (recs[i].EventType == KEY_EVENT) {
                    const KEY_EVENT_RECORD& kr = recs[i].Event.KeyEvent;
                    if (kr.bKeyDown && (kr.uChar.AsciiChar != 0 || kr.wVirtualKeyCode == VK_RETURN)) {
                        hasPrintableKey = true;
                        break;
                    }
                }
            }
            if (!hasPrintableKey) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            std::string line;
            if (!std::getline(std::cin, line))
                break;

            cmdtool.HandleInputLine(line.c_str());
            if (!running.load(std::memory_order_relaxed))
                break;
        }
    }
#else
    while (running.load(std::memory_order_relaxed)) {
        if (create_admin_exclusive.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        fd_set rfds;
        FD_ZERO(&rfds);
        FD_SET(STDIN_FILENO, &rfds);
        timeval tv{ 0, 50 * 1000 };
        int rv = select(STDIN_FILENO + 1, &rfds, nullptr, nullptr, &tv);
        if (rv > 0 && FD_ISSET(STDIN_FILENO, &rfds)) {
            std::string line;
            if (!std::getline(std::cin, line))
                break;

            cmdtool.HandleInputLine(line.c_str());
            if (!running.load(std::memory_order_relaxed))
                break;
}
    }
#endif

    if (running.exchange(false, std::memory_order_relaxed))
        server.Stop();

    cmdtool.Stop();

    if (server_thread.joinable())
        server_thread.join();
}