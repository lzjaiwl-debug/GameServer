#pragma once
// 进程间「数据契约」。★ 与前端 TS 的 AuthMessages 字段表是同一套规则：
//   全 big-endian / string = u32 长度 + 字节 / vector = u32 个数 + 元素 /
//   用户类型按 Serialize() 里的字段顺序，无 tag 无版本号 → 字段顺序就是协议，改顺序不报错只错位。

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <AssetLibrary/SerializerFactory.hpp>

namespace gs {

// ---------------- 节点类型 ----------------
enum NodeType : uint16_t {
    NODE_UNKNOWN = 0,
    NODE_CENTER  = 1,   // 中心服：注册发现 / 全局 ID / 配置
    NODE_GATE    = 2,   // 网关服：对外唯一入口
    NODE_GAME    = 3,   // 游戏逻辑服
    NODE_DBAGENT = 4,   // 数据代理服（唯一碰库）
    NODE_LOG     = 5,   // 日志服
};

inline const char* NodeTypeName(uint16_t t) {
    switch (t) {
    case NODE_CENTER:  return "Center";
    case NODE_GATE:    return "Gate";
    case NODE_GAME:    return "Game";
    case NODE_DBAGENT: return "DBAgent";
    case NODE_LOG:     return "Log";
    default:           return "Unknown";
    }
}

// ---------------- 消息号 ----------------
//   0x0001 - 0x7FFF  业务消息（客户端 <-> Gate <-> Game）
//   0x8000 - 0xFFFF  内部进程间控制消息
// ★ 对外（客户端）链路必须避开 0x3000-0x37FF（12288-14335）：
//   u16 消息号按 BE 写入时首字节 = 高字节，落在 0x30-0x37 会被 engine.io 的
//   get_frame() 误判成帧类型字符。内部 TCP 长度前缀帧不受此限制。
namespace MsgId {
enum : uint16_t {
    // ---- 内部：节点管理（0x80xx）----
    NODE_REGISTER = 0x8001,   // 进程上线注册（C -> S）
    NODE_TABLE    = 0x8002,   // 地址表全量下发（S -> C）
    NODE_ONLINE   = 0x8003,   // 某节点上线（S -> C 广播）
    NODE_OFFLINE  = 0x8004,   // 某节点下线（S -> C 广播）
    HEARTBEAT     = 0x8005,   // 心跳（双向）

    // ---- 内部：全局 ID（0x81xx）----
    ALLOC_ID_REQ  = 0x8101,   // 申请一段 ID
    ALLOC_ID_RES  = 0x8102,   // 返回首值 + 数量

    // ---- 业务占位（待前后端共同定表）----
    // C2S_LOGIN_REQ = 0x0101,
    // S2C_LOGIN_RES = 0x0102,
};
}

// ---------------- 帧标志 ----------------
enum FrameFlag : uint8_t {
    FF_NONE     = 0x00,
    FF_REQ      = 0x01,   // 请求
    FF_RES      = 0x02,   // 响应
    FF_NEED_ACK = 0x04,   // 需要对端确认
    FF_COMPRESS = 0x08,   // 负载已压缩
};

// ---------------- 消息体 ----------------
struct NodeInfo {
    uint16_t    type = 0;      // NodeType
    uint16_t    id   = 0;      // 同类节点内的序号（从 1 开始）
    std::string name;          // 可读名，如 "gate-1"
    std::string host;          // 该节点对外监听地址（供别的节点来连）
    uint16_t    port = 0;      // 该节点对外监听端口

    void Serialize(AssetLib::SerializerFactory& s) { s % type % id % name % host % port; }
};

// NODE_REGISTER
struct MsgRegister {
    NodeInfo info;
    void Serialize(AssetLib::SerializerFactory& s) { s % info; }
};

// NODE_TABLE
struct MsgNodeTable {
    std::vector<NodeInfo> nodes;
    void Serialize(AssetLib::SerializerFactory& s) { s % nodes; }
};

// NODE_ONLINE / NODE_OFFLINE（携带发生变化的那个节点）
struct MsgNodeChange {
    NodeInfo info;
    void Serialize(AssetLib::SerializerFactory& s) { s % info; }
};

// HEARTBEAT
struct MsgHeartbeat {
    uint32_t seq = 0;
    void Serialize(AssetLib::SerializerFactory& s) { s % seq; }
};

// ALLOC_ID_REQ：申请 count 个 kind 类 ID（kind 预留：1=角色 2=物品 3=流水）
struct MsgAllocIdReq {
    uint16_t kind  = 0;
    uint32_t count = 0;
    void Serialize(AssetLib::SerializerFactory& s) { s % kind % count; }
};

// ALLOC_ID_RES：返回 [first, first+count) 这一段
struct MsgAllocIdRes {
    uint16_t kind  = 0;
    uint64_t first = 0;
    uint32_t count = 0;
    void Serialize(AssetLib::SerializerFactory& s) { s % kind % first % count; }
};

// ---------------- 序列化小工具 ----------------
template <class T>
inline bool PackTo(T& msg, std::vector<uint8_t>& out) {
    AssetLib::BinaryData bin;
    AssetLib::SerializerFactory::Serialize(msg, bin);
    if (!bin.data || bin.size == 0) return false;
    out.assign((const uint8_t*)bin.data, (const uint8_t*)bin.data + bin.size);
    return true;
}

template <class T>
inline bool UnpackFrom(T& msg, const void* data, size_t len) {
    if (!data || len == 0) return false;
    AssetLib::BinaryData bin(data, len);
    try {
        AssetLib::SerializerFactory::Deserialize(msg, bin);
    } catch (...) {
        return false;
    }
    return true;
}

} // namespace gs
