#include "Frame.h"
#include <cstring>

namespace gs {

namespace {
inline void PutBE16(std::vector<uint8_t>& v, uint16_t x) {
    v.push_back((uint8_t)(x >> 8)); v.push_back((uint8_t)(x & 0xFF));
}
inline void PutBE32(std::vector<uint8_t>& v, uint32_t x) {
    v.push_back((uint8_t)(x >> 24)); v.push_back((uint8_t)((x >> 16) & 0xFF));
    v.push_back((uint8_t)((x >> 8) & 0xFF)); v.push_back((uint8_t)(x & 0xFF));
}
inline uint16_t GetBE16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
inline uint32_t GetBE32(const uint8_t* p) {
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}
}

std::vector<uint8_t> EncodeFrame(uint16_t msgId, uint8_t flag, const void* payload, size_t len) {
    std::vector<uint8_t> out;
    out.reserve(kFrameHeaderSize + len);
    PutBE32(out, (uint32_t)(3 + len));   // payload_len = msgId(2) + flag(1) + 负载
    PutBE16(out, msgId);
    out.push_back(flag);
    if (len && payload) {
        const uint8_t* p = (const uint8_t*)payload;
        out.insert(out.end(), p, p + len);
    }
    return out;
}

void FrameDecoder::Feed(const uint8_t* data, size_t len) {
    if (!data || len == 0) return;
    buf_.insert(buf_.end(), data, data + len);
}

bool FrameDecoder::TryPop(Frame& out) {
    if (buf_.size() - head_ < 4) return false;
    const uint8_t* base = buf_.data() + head_;
    uint32_t payloadLen = GetBE32(base);
    if (payloadLen < 3 || payloadLen > kMaxFramePayload) {
        Reset();                      // 协议被破坏，丢弃全部缓冲，等重新同步
        return false;
    }
    size_t total = 4 + (size_t)payloadLen;
    if (buf_.size() - head_ < total) return false;   // 半包

    const uint8_t* body = base + 4;
    out.msgId  = GetBE16(body);
    out.flag   = body[2];
    out.payload.assign(body + 3, body + 3 + (payloadLen - 3));
    head_ += total;
    return true;
}

bool FrameDecoder::Next(Frame& out) {
    if (TryPop(out)) {
        // 消费完一帧后，若缓冲已全消费完则清空，避免长期持有大 vector
        if (head_ >= buf_.size()) { buf_.clear(); head_ = 0; }
        return true;
    }
    // 长时间没凑齐一帧时，把已消费的部分挪掉，避免缓冲无界增长
    if (head_ > 0 && head_ > 64 * 1024) {
        buf_.erase(buf_.begin(), buf_.begin() + head_);
        head_ = 0;
    }
    return false;
}

void FrameDecoder::Reset() { buf_.clear(); head_ = 0; }

size_t FrameDecoder::Pending() const { return buf_.size() - head_; }

} // namespace gs
