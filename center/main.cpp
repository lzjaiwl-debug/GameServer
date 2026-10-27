// CenterServer —— 服务注册与发现 / 全局唯一 ID / 配置下发
// 它是其余进程（Gate / Game / DBAgent）的启动地基：
//   各进程启动 → 连 Center 注册自己 → 拉回地址表 → 再去连自己的依赖。
#include <Socket.h>
#include <NodeServer.h>
#include <NodeClient.h>
#include <MsgDef.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <unordered_map>

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
            g_running.store(false);
            return TRUE;
        }
        return FALSE;
    }, TRUE);
#else
    ::signal(SIGINT, [](int) { g_running.store(false); });
    ::signal(SIGTERM, [](int) { g_running.store(false); });
#endif
}

// yyyy-mm-dd hh:mm:ss.mmm
std::string NowStr() {
    auto now  = std::chrono::system_clock::now();
    auto t    = std::chrono::system_clock::to_time_t(now);
    auto ms   = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d:%02d.%03d",
                  tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                  tm.tm_hour, tm.tm_min, tm.tm_sec, (int)ms);
    return buf;
}

void Log(const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    std::printf("[%s] ", NowStr().c_str());
    std::vprintf(fmt, ap);
    std::printf("\n");
    std::fflush(stdout);
    va_end(ap);
}

// ---------------- Center 状态 ----------------
struct Registry {
    // connId -> 已注册节点信息
    std::unordered_map<uint32_t, NodeInfo> nodes;
    // 每类节点的下一个可用 id
    std::unordered_map<uint16_t, uint16_t> nextId;
    // 全局 ID 分配：kind -> 下一个可用值
    std::unordered_map<uint16_t, uint64_t> nextGlobalId;

    uint16_t AllocNodeId(uint16_t type) {
        uint16_t id = nextId[type] ? nextId[type] : 1;
        nextId[type] = id + 1;
        return id;
    }

    uint64_t AllocGlobalId(uint16_t kind, uint32_t count) {
        uint64_t first = nextGlobalId.count(kind) ? nextGlobalId[kind] : 1;
        if (count == 0) count = 1;
        nextGlobalId[kind] = first + count;
        return first;
    }

    std::vector<NodeInfo> Snapshot() const {
        std::vector<NodeInfo> v;
        v.reserve(nodes.size());
        for (auto& kv : nodes) v.push_back(kv.second);
        return v;
    }
};

} // namespace

int main(int argc, char** argv) {
    int port = 9000;
    if (argc >= 2) port = std::atoi(argv[1]);

    InstallStopHandler();
    if (!NetInit()) { Log("NetInit failed"); return 1; }

    NodeServer svr;
    if (!svr.Start("0.0.0.0", port)) {
        Log("listen 0.0.0.0:%d failed", port);
        return 1;
    }
    Log("CenterServer listening on 0.0.0.0:%d", svr.Port());

    Registry reg;
    uint32_t hbCount = 0;

    while (g_running.load()) {
        Incoming ev;
        bool got = false;
        while (svr.Poll(ev)) {
            got = true;

            if (ev.kind == LinkEvent::Open) {
                Log("conn#%u opened (%zu total)", ev.connId, svr.ConnCount());
                continue;
            }

            if (ev.kind == LinkEvent::Close) {
                auto it = reg.nodes.find(ev.connId);
                if (it != reg.nodes.end()) {
                    MsgNodeChange chg{ it->second };
                    std::vector<uint8_t> body;
                    if (PackTo(chg, body))
                        svr.Broadcast(MsgId::NODE_OFFLINE, FF_NONE, body.data(), body.size());
                    Log("node offline: %s-%u (conn#%u)",
                        NodeTypeName(it->second.type), it->second.id, ev.connId);
                    reg.nodes.erase(it);
                } else {
                    Log("conn#%u closed", ev.connId);
                }
                continue;
            }

            // ---- 帧 ----
            switch (ev.frame.msgId) {

            case MsgId::NODE_REGISTER: {
                MsgRegister req{};
                if (!UnpackFrom(req, ev.frame.payload.data(), ev.frame.payload.size())) {
                    Log("conn#%u: bad NODE_REGISTER payload, kick", ev.connId);
                    svr.CloseConn(ev.connId);
                    break;
                }
                NodeInfo info = req.info;
                info.id = reg.AllocNodeId(info.type);
                if (info.name.empty())
                    info.name = std::string(NodeTypeName(info.type)) + "-" + std::to_string(info.id);

                reg.nodes[ev.connId] = info;
                Log("node register: %s id=%u %s:%u (conn#%u)",
                    NodeTypeName(info.type), info.id, info.host.c_str(), info.port, ev.connId);

                // 回该节点：全量地址表
                MsgNodeTable table{};
                table.nodes = reg.Snapshot();
                std::vector<uint8_t> body;
                if (PackTo(table, body))
                    svr.Send(ev.connId, MsgId::NODE_TABLE, FF_NONE, body.data(), body.size());

                // 广播其它节点：有节点上线
                MsgNodeChange chg{ info };
                body.clear();
                if (PackTo(chg, body))
                    svr.Broadcast(MsgId::NODE_ONLINE, FF_NONE, body.data(), body.size());
                break;
            }

            case MsgId::HEARTBEAT: {
                ++hbCount;
                if (hbCount % 50 == 0) Log("heartbeat x%u", hbCount);
                break;
            }

            case MsgId::ALLOC_ID_REQ: {
                MsgAllocIdReq req{};
                if (!UnpackFrom(req, ev.frame.payload.data(), ev.frame.payload.size())) break;
                MsgAllocIdRes res{};
                res.kind  = req.kind;
                res.count = req.count ? req.count : 1;
                res.first = reg.AllocGlobalId(req.kind, res.count);
                std::vector<uint8_t> body;
                if (PackTo(res, body))
                    svr.Send(ev.connId, MsgId::ALLOC_ID_RES, FF_RES, body.data(), body.size());
                Log("alloc id kind=%u -> [%llu, %llu)", req.kind,
                    (unsigned long long)res.first,
                    (unsigned long long)(res.first + res.count));
                break;
            }

            default:
                Log("conn#%u: unknown msg 0x%04X (%zu bytes)",
                    ev.connId, ev.frame.msgId, ev.frame.payload.size());
                break;
            }
        }

        if (!got) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    Log("shutting down...");
    svr.Stop();
    NetCleanup();
    return 0;
}
