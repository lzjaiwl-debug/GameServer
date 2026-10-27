#pragma once
// 内部进程间帧格式（裸 TCP，粘包用长度前缀解决）：
//   [u32 payload_len][u16 msg_id][u8 flag][payload...]
//   payload_len 不含自身，= 2 + 1 + payload.size()
// 全部 big-endian。

#include <cstdint>
#include <vector>

namespace gs {

constexpr uint32_t kFrameHeaderSize = 7;   // 4 + 2 + 1
constexpr uint32_t kMaxFramePayload = 8 * 1024 * 1024; // 8MB，防异常包打爆内存

struct Frame {
    uint16_t             msgId = 0;
    uint8_t              flag  = 0;
    std::vector<uint8_t> payload;
};

// 编码一帧（含长度前缀）
std::vector<uint8_t> EncodeFrame(uint16_t msgId, uint8_t flag, const void* payload, size_t len);

// 增量解码器：把收到的字节流喂进来，循环 Next() 取完整帧。
// 半包会留在内部缓冲里等下一批数据。
class FrameDecoder {
public:
    void Feed(const uint8_t* data, size_t len);
    bool Next(Frame& out);          // 取到一帧返回 true
    void Reset();
    size_t Pending() const;         // 缓冲里未解出的字节数

private:
    bool TryPop(Frame& out);

    std::vector<uint8_t> buf_;
    size_t               head_ = 0; // 已消费到的位置
};

} // namespace gs
