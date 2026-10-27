// TestNode —— CenterServer 的联调用测试进程。
// 它扮演任意一个游戏进程：连 Center → 注册 → 收地址表 → 看上下线广播 → 申请 ID。
// 用法：TestNode [type 1=Center 2=Gate 3=Game 4=DBAgent] [name] [host] [port]
#include <Socket.h>
#include <NodeClient.h>
#include <MsgDef.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#else
#include <csignal>
#endif

using namespace gs;

namespace {

std::atomic<bool> g_running{ true };

void InstallStopHandler() {
#ifdef _WIN32
    ::SetConsoleCtrlHandler([](DWORD ctrl) -> BOOL {
        if (ctrl == CTRL_C_EVENT || ctrl == CTRL_CLOSE_EVENT || ctrl == CTRL_SHUTDOWN_EVENT) {
            g_running.store(false); return TRUE;
        }
        return FALSE;
    }, TRUE);
#else
    ::signal(SIGINT, [](int) { g_running.store(false); });
    ::signal(SIGTERM, [](int) { g_running.store(false); });
#endif
}

void Log(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    std::printf("[TestNode] ");
    std::vprintf(fmt, ap);
    std::printf("\n"); std::fflush(stdout);
    va_end(ap);
}

} // namespace

int main(int argc, char** argv) {
    uint16_t    type = NODE_GAME;
    std::string name = "game-1";
    std::string host = "127.0.0.1";
    int         port = 9000;

    if (argc >= 2) type = (uint16_t)std::atoi(argv[1]);
    if (argc >= 3) name = argv[2];
    if (argc >= 4) host = argv[3];
    if (argc >= 5) port = std::atoi(argv[4]);

    InstallStopHandler();
    if (!NetInit()) { Log("NetInit failed"); return 1; }

    NodeClient cli;
    if (!cli.Start(host.c_str(), port, 5000)) { Log("start failed"); return 1; }
    Log("connecting to %s:%d as %s(type=%u)...", host.c_str(), port, name.c_str(), type);

    bool        registered = false;
    uint32_t    hb         = 0;
    auto        lastAlloc  = std::chrono::steady_clock::now();

    while (g_running.load()) {
        Incoming ev;
        bool got = false;

        while (cli.Poll(ev)) {
            got = true;
            if (ev.kind == LinkEvent::Open) {
                Log("connected -> sending NODE_REGISTER");
                MsgRegister req{};
                req.info.type = type;
                req.info.name = name;
                req.info.host = "127.0.0.1";   // 本节点对外地址（这里是占位）
                req.info.port = 0;
                std::vector<uint8_t> body;
                if (PackTo(req, body)) cli.Send(MsgId::NODE_REGISTER, FF_REQ, body.data(), body.size());
            } else if (ev.kind == LinkEvent::Close) {
                Log("disconnected, will retry...");
                registered = false;
            } else {
                switch (ev.frame.msgId) {
                case MsgId::NODE_TABLE: {
                    MsgNodeTable t{};
                    if (!UnpackFrom(t, ev.frame.payload.data(), ev.frame.payload.size())) {
                        Log("bad NODE_TABLE"); break;
                    }
                    registered = true;
                    Log("=== NODE_TABLE (%zu nodes) ===", t.nodes.size());
                    for (auto& n : t.nodes)
                        Log("  %-8s id=%-3u %s:%u", NodeTypeName(n.type), n.id, n.host.c_str(), n.port);
                    break;
                }
                case MsgId::NODE_ONLINE: {
                    MsgNodeChange c{};
                    if (UnpackFrom(c, ev.frame.payload.data(), ev.frame.payload.size()))
                        Log(">>> online : %s-%u %s:%u", NodeTypeName(c.info.type), c.info.id, c.info.host.c_str(), c.info.port);
                    break;
                }
                case MsgId::NODE_OFFLINE: {
                    MsgNodeChange c{};
                    if (UnpackFrom(c, ev.frame.payload.data(), ev.frame.payload.size()))
                        Log("<<< offline: %s-%u", NodeTypeName(c.info.type), c.info.id);
                    break;
                }
                case MsgId::HEARTBEAT:
                    ++hb;
                    break;
                case MsgId::ALLOC_ID_RES: {
                    MsgAllocIdRes r{};
                    if (UnpackFrom(r, ev.frame.payload.data(), ev.frame.payload.size()))
                        Log("alloc res: kind=%u first=%llu count=%u",
                            r.kind, (unsigned long long)r.first, r.count);
                    break;
                }
                default:
                    Log("unknown msg 0x%04X", ev.frame.msgId);
                    break;
                }
            }
        }

        // 每 10 秒申请一段角色 ID，验证 ALLOC_ID 通路
        auto now = std::chrono::steady_clock::now();
        if (registered && std::chrono::duration_cast<std::chrono::seconds>(now - lastAlloc).count() >= 10) {
            MsgAllocIdReq req{}; req.kind = 1; req.count = 100;
            std::vector<uint8_t> body;
            if (PackTo(req, body)) cli.Send(MsgId::ALLOC_ID_REQ, FF_REQ, body.data(), body.size());
            lastAlloc = now;
        }

        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Log("stopping (heartbeat seen=%u)...", hb);
    cli.Stop();
    NetCleanup();
    return 0;
}
