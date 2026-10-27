#pragma once
#include <regex>

namespace NetLibrary {
    namespace Httplib {

        // 结果状态：支持常规(Content-Length)与 chunked 两类完整性判断
        struct HttpRecvStatus {
            bool header_complete = false;   // 是否已完整接收到首部（含 \r\n\r\n）
            bool message_complete = false;  // 是否已完整接收到整个HTTP消息
            bool is_chunked = false;        // 是否为 Transfer-Encoding: chunked
            size_t header_end = 0;          // 头部结束偏移（body起始位置）
            size_t need_more = 0;           // 若不完整，至少还需要多少字节（估计值）
            size_t extra_tail = 0;          // 若有粘包，尾部多余字节数
        };

        inline size_t FindHeaderEnd(const std::string& s) {
            auto pos = s.find("\r\n\r\n");
            return pos == std::string::npos ? (size_t)-1 : (pos + 4);
        }

        inline std::string TrimSpaces(std::string s) {
            const auto l = s.find_first_not_of(" \t\r\n");
            if (l == std::string::npos) return {};
            const auto r = s.find_last_not_of(" \t\r\n");
            return s.substr(l, r - l + 1);
        }

        inline bool HeaderHasChunked(const std::string& headers) {
            // 查找 Transfer-Encoding: chunked（大小写不敏感）
            auto pos = headers.find("Transfer-Encoding");
            if (pos == std::string::npos) pos = headers.find("transfer-encoding");
            if (pos == std::string::npos) return false;
            auto colon = headers.find(':', pos);
            if (colon == std::string::npos) return false;
            auto eol = headers.find("\r\n", colon);
            std::string v = TrimSpaces(headers.substr(colon + 1, (eol == std::string::npos ? headers.size() : eol) - (colon + 1)));
            std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
            return v.find("chunked") != std::string::npos;
        }

        inline long ParseContentLength(const std::string& headers) {
            auto pos = headers.find("Content-Length");
            if (pos == std::string::npos) pos = headers.find("content-length");
            if (pos == std::string::npos) return -1;
            auto colon = headers.find(':', pos);
            if (colon == std::string::npos) return -1;
            auto eol = headers.find("\r\n", colon);
            auto val = TrimSpaces(headers.substr(colon + 1, (eol == std::string::npos ? headers.size() : eol) - (colon + 1)));
            // 仅数字
            long n = 0;
            for (char c : val) {
                if (!std::isdigit(static_cast<unsigned char>(c))) return -1;
                n = n * 10 + (c - '0');
            }
            return n;
        }

        // 解析 chunked 正文，尽可能前进；返回是否完整，及消费的字节数（不含首部）
        inline bool DecodeChunkedScan(const char* body, size_t body_len, size_t& consumed) {
            consumed = 0;
            while (true) {
                // 查找尺寸行
                const char* p = body + consumed;
                const char* end = body + body_len;
                const char* crlf = nullptr;
                for (const char* q = p; q + 1 < end; ++q) {
                    if (q[0] == '\r' && q[1] == '\n') { crlf = q; break; }
                }
                if (!crlf) return false; // 尺寸行不完整
                std::string size_hex(p, crlf);
                // 去除扩展
                auto semi = size_hex.find(';');
                if (semi != std::string::npos) size_hex = size_hex.substr(0, semi);
                size_hex = TrimSpaces(size_hex);
                // 解析十六进制
                size_t chunk_size = 0;
                {
                    std::istringstream iss(size_hex);
                    iss >> std::hex >> chunk_size;
                    if (!iss || iss.fail()) return false;
                }
                consumed = (crlf - body) + 2;
                // 终止块
                if (chunk_size == 0) {
                    // 读取 trailer 直到空行
                    while (true) {
                        // 需要至少两个字节
                        if (consumed + 2 > body_len) return false;
                        // 查找下一行
                        const char* lp = body + consumed;
                        const char* ln = nullptr;
                        for (const char* q = lp; q + 1 < end; ++q) {
                            if (q[0] == '\r' && q[1] == '\n') { ln = q; break; }
                        }
                        if (!ln) return false;
                        if (ln == lp) { // 空行 -> 结束
                            consumed += 2;
                            return true;
                        }
                        consumed = (ln - body) + 2; // 跳过一条 trailer
                    }
                }
                // 需要 chunk_size + CRLF
                if (consumed + chunk_size + 2 > body_len) return false; // 数据不足
                consumed += chunk_size;
                // 校验 CRLF
                if (body[consumed] != '\r' || (consumed + 1 > body_len) || body[consumed + 1] != '\n') return false;
                consumed += 2;
                // 继续下一块
            }
        }
        // 从头部提取 multipart boundary（支持 boundary="xxx" 或 boundary=xxx）
        inline std::string ExtractBoundaryFromHeaders(const std::string& headers) {
            auto pos = headers.find("Content-Type");
            if (pos == std::string::npos) pos = headers.find("content-type");
            if (pos == std::string::npos) return {};
            auto colon = headers.find(':', pos);
            if (colon == std::string::npos) return {};
            auto eol = headers.find("\r\n", colon);
            std::string ct = TrimSpaces(headers.substr(colon + 1, (eol == std::string::npos ? headers.size() : eol) - (colon + 1)));
            // 找 boundary=
            auto bpos = ct.find("boundary=");
            if (bpos == std::string::npos) return {};
            std::string b = ct.substr(bpos + 9);
            // 去引号
            if (!b.empty() && b.front() == '"') {
                if (b.size() >= 2 && b.back() == '"') b = b.substr(1, b.size() - 2);
            }
            return b;
        }

        // 扫描一个完整的 multipart 段（基于 boundary），返回是否找到完整段以及消费的字节数（不含HTTP头）
        // 格式：--boundary CRLF [headers CRLF]* CRLF data CRLF --boundary 或 --boundary--
        // 注意：此函数只消费一个段（不含下一段起始的 --boundary），便于“每段作为独立消息”
        inline bool ScanOneMultipartPart(const char* body, size_t body_len, const std::string& boundary, size_t& consumed) {
            consumed = 0;
            if (boundary.empty()) return false;
            std::string dashb = std::string("--") + boundary;

            // 寻找当前段的起始 --boundary
            // body 可能直接从 boundary 开始，也可能前面有换行
            const char* start = nullptr;
            {
                // 允许 body 开头就是 --boundary 或以 CRLF 后跟 --boundary
                if (body_len >= dashb.size() && std::memcmp(body, dashb.data(), dashb.size()) == 0) {
                    start = body;
                }
                else {
                    // 查找下一处 --boundary
                    const char* endp = body + body_len;
                    for (const char* p = body; p + dashb.size() <= endp; ++p) {
                        if (*p == '-' && std::memcmp(p, dashb.data(), dashb.size()) == 0) { start = p; break; }
                    }
                }
            }
            if (!start) return false;

            size_t offset = (size_t)(start - body);
            // 检查是否终止边界（--boundary--）
            if (offset + dashb.size() + 2 <= body_len && std::memcmp(start + dashb.size(), "--", 2) == 0) {
                // 终止边界：可以视为一个“空段”，需跟随 CRLF 结束
                size_t endNeeded = dashb.size() + 2 + 2; // "--" + CRLF
                if (offset + endNeeded > body_len) return false;
                consumed = offset + endNeeded;
                return true;
            }

            // 期望紧随的是 CRLF
            if (offset + dashb.size() + 2 > body_len || std::memcmp(start + dashb.size(), "\r\n", 2) != 0)
                return false;

            size_t cursor = offset + dashb.size() + 2;

            // part headers：直到空行 CRLFCRLF
            // 寻找 "\r\n\r\n"
            const char* hb = body + cursor;
            const char* hend = nullptr;
            {
                const char* endp = body + body_len;
                for (const char* p = hb; p + 3 < endp; ++p) {
                    if (p[0] == '\r' && p[1] == '\n' && p[2] == '\r' && p[3] == '\n') { hend = p; break; }
                }
            }
            if (!hend) return false;
            cursor = (size_t)(hend - body) + 4; // 跳过 CRLFCRLF

            // part data：直到下一个 "--boundary"（data 前可能有任意字节）
            // 找下一处边界
            const char* nextb = nullptr;
            {
                const char* endp = body + body_len;
                for (const char* p = body + cursor; p + dashb.size() <= endp; ++p) {
                    if (*p == '-' && std::memcmp(p, dashb.data(), dashb.size()) == 0) { nextb = p; break; }
                }
            }
            if (!nextb) return false;

            // 数据到 nextb 之前，通常以 CRLF 结尾；若有，去掉
            size_t data_end = (size_t)(nextb - body);
            if (data_end >= 2 && std::memcmp(body + data_end - 2, "\r\n", 2) == 0)
                data_end -= 2;

            // 一个完整段的消费范围：从当前 start 到 data_end 的数据，再加上后续的 boundary 起始不消费（留作下一消息）
            consumed = data_end; // 消费到数据末尾（不包含尾部CRLF与下一boundary）
            return true;
        }


        // 在 chunked 的“线缆字节流”中，边解码、边按 boundary 扫描一个完整的 multipart part。
        // 成功时：返回 true，并将 wire_consumed 设置为应消费的线缆字节数（不含下一段的 "--boundary" 起始），
        // 使得调用方能保留下一段的 boundary 起始到缓冲中作为“下一条独立消息”的开头。
        inline bool ScanMultipartPartInChunkedWire(const char* wire, size_t wire_len, const std::string& boundary, size_t& wire_consumed)
        {
            wire_consumed = 0;
            if (!wire || wire_len == 0 || boundary.empty()) return false;

            const std::string dashb = std::string("--") + boundary;

            // 找到实体解码流中第一个 boundary 的“解码偏移”（dec_off_first_b）
            // 然后解析 part 头（至 CRLFCRLF），再找到下一处 boundary 的“解码偏移”（dec_off_next_b），
            // 并将 dec_off_next_b 映射为 wire_consumed（线缆消费点停止在下一 boundary 开始之前的 CRLF 处）。
            size_t idx = 0;                    // wire 游标
            size_t dec_total = 0;              // 已解码的总字节数（实体字节）
            size_t first_b_dec_off = SIZE_MAX; // 第一个 boundary 在“解码流”中的偏移
            size_t headers_end_dec_off = SIZE_MAX;
            size_t next_b_dec_off = SIZE_MAX;

            // 辅助：从 wire 的 idx 开始解析一行，返回行末 CRLF 后的位置和行内容（不含 CRLF）
            auto read_line = [&](size_t from, std::string& out, size_t& after_crlf) -> bool {
                out.clear();
                size_t p = from;
                while (p + 1 < wire_len) {
                    if (wire[p] == '\r' && wire[p + 1] == '\n') {
                        after_crlf = p + 2;
                        return true;
                    }
                    out.push_back(wire[p]);
                    ++p;
                }
                return false; // 不完整
            };

            // KMP/朴素都可，这里朴素：在“解码偏移区间 [base_dec, base_dec + len)”内寻找 dashb
            auto find_boundary_in_dec_window = [&](const char* chunk_data, size_t chunk_len, size_t base_dec, size_t search_from_dec, size_t& found_dec_off) -> bool {
                // search_from_dec 是“解码偏移”的起点；将其换算到 chunk_data 的本块内偏移
                if (search_from_dec < base_dec) search_from_dec = base_dec;
                if (search_from_dec >= base_dec + chunk_len) return false;
                size_t start_in_chunk = search_from_dec - base_dec;
                // 允许跨 chunk 的匹配，这里简单处理：仅在本块内匹配，边界跨块时留待下个块继续匹配
                for (size_t i = start_in_chunk; i + dashb.size() <= chunk_len; ++i) {
                    if (std::memcmp(chunk_data + i, dashb.data(), dashb.size()) == 0) {
                        found_dec_off = base_dec + i;
                        return true;
                    }
                }
                return false;
            };

            // 第一步：定位第一个 boundary（起点），并解析到 part 头结束（CRLFCRLF）
            // 第二步：继续扫描数据至下一 boundary（作为本 part 的结束）。
            size_t search_from_dec = 0; // 在解码流中的搜索起点

            while (true) {
                // 解析 chunk-size 行
                if (idx >= wire_len) return false;
                std::string size_hex;
                size_t after_size_crlf = 0;
                if (!read_line(idx, size_hex, after_size_crlf)) {
                    return false; // 需要更多数据
                }
                // 去除扩展
                auto semi = size_hex.find(';');
                if (semi != std::string::npos) size_hex = size_hex.substr(0, semi);
                size_hex = TrimSpaces(size_hex);

                size_t chunk_size = 0;
                {
                    std::istringstream iss(size_hex);
                    iss >> std::hex >> chunk_size;
                    if (!iss || iss.fail()) return false; // 非法或不完整
                }
                idx = after_size_crlf;
                // 需要 chunk_size + CRLF
                if (idx + chunk_size + 2 > wire_len) return false;
                const char* chunk_data = wire + idx;

                // 在本 chunk 的解码窗口上查找 boundary 或者 CRLFCRLF（解析 part 头）
                // 1) 找到第一个 boundary
                if (first_b_dec_off == SIZE_MAX) {
                    size_t found = SIZE_MAX;
                    if (find_boundary_in_dec_window(chunk_data, chunk_size, dec_total, search_from_dec, found)) {
                        first_b_dec_off = found;
                        // boundary 后必须紧跟 CRLF
                        size_t after_boundary_dec = first_b_dec_off + dashb.size();
                        // 边界行可能是 "--boundary--"（终止），但第一处若即为终止则没有有效 part
                        bool is_final = false;
                        if (after_boundary_dec + 2 <= dec_total + chunk_size) {
                            size_t in_chunk_off = after_boundary_dec - dec_total;
                            if (in_chunk_off + 2 <= chunk_size && std::memcmp(chunk_data + in_chunk_off, "--", 2) == 0) {
                                is_final = true;
                            }
                        }
                        // 跳过 CRLF（若不是终止）
                        if (!is_final) {
                            // boundary 后应为 CRLF，再跟 part headers（直到 CRLFCRLF）
                            // 可能跨块，这里逐字节推进，直到在“解码流”里找到 "\r\n\r\n"
                            // 简化：把本 chunk 数据先看一遍，若没找到，再等下个 chunk 继续
                        }
                        search_from_dec = first_b_dec_off + dashb.size(); // 继续从 boundary 之后查找
                    }
                }
                // 2) 若已定位 boundary，则继续找 headers 结束（\r\n\r\n）
                if (first_b_dec_off != SIZE_MAX && headers_end_dec_off == SIZE_MAX) {
                    // 在当前 chunk 数据内查找从 search_from_dec 开始的 "\r\n\r\n"
                    // 简化：朴素扫描
                    size_t base = dec_total;
                    size_t start = search_from_dec > base ? (search_from_dec - base) : 0;
                    for (size_t i = start; i + 3 < chunk_size; ++i) {
                        if (chunk_data[i] == '\r' && chunk_data[i + 1] == '\n' && chunk_data[i + 2] == '\r' && chunk_data[i + 3] == '\n') {
                            headers_end_dec_off = base + i + 4;
                            search_from_dec = headers_end_dec_off;
                            break;
                        }
                    }
                }
                // 3) 若已到 headers 结束，则继续找“下一处 boundary”（即本 part 的结束）
                if (headers_end_dec_off != SIZE_MAX && next_b_dec_off == SIZE_MAX) {
                    size_t found = SIZE_MAX;
                    if (find_boundary_in_dec_window(chunk_data, chunk_size, dec_total, search_from_dec, found)) {
                        next_b_dec_off = found;
                        // 数据可能以 CRLF 结束，若存在，按惯例去掉（只影响“解码视图”，wire 对齐由下方计算）
                        // 将“解码结束偏移”映射到 wire_consumed：停止在“下一 boundary 起始”的 wire 位置。
                        // 计算本 chunk 的 wire 区段起点（数据起点）
                        size_t dec_needed_in_this_chunk = next_b_dec_off - dec_total; // boundary 起点在本 chunk 的偏移
                        wire_consumed = idx /*数据起点*/ + dec_needed_in_this_chunk;
                        return true;
                    }
                }

                // 推进到本 chunk 末尾（实体数据）
                dec_total += chunk_size;
                idx += chunk_size;
                // 校验 chunk CRLF
                if (wire[idx] != '\r' || wire[idx + 1] != '\n') return false;
                idx += 2;

                // 终止块：若 chunk_size == 0，在上面已 return false（因为没有完整 part）或由上层处理 trailer
                if (chunk_size == 0) return false;

                // 继续下一个 chunk
                if (search_from_dec < dec_total) search_from_dec = dec_total;
            }
        }
        // 核心方法：判断接收到的数据是否构成完整HTTP消息；支持 GET/POST 常规(Content-Length)与 chunked
// data/len 为当前一次接收缓存（可包含粘包），extra_tail_len* 返回尾部粘包字节数（若存在）。
// 改进：chunked 支持两种模式：
// 1) 独立块模式：每个 <hex>\r\n<data>\r\n 作为一个完整单元返回（流式）
// 2) 终止块模式：若读取到 0 终止块与 trailer 空行，则整条消息完整
        inline HttpRecvStatus CheckHttpReceiveCompleteness(const char* data, size_t len, size_t* extra_tail_len)
        {
            HttpRecvStatus st{};
            if (extra_tail_len) *extra_tail_len = 0;
            if (!data || len == 0) { st.need_more = 1; return st; }

            std::string s(data, len);
            size_t hdrEnd = FindHeaderEnd(s);

            if (hdrEnd == (size_t)-1) {
                // 头未完整：先尝试“无头单段 chunk 样式”
                st.header_complete = false;
                st.header_end = 0;

                const char* p = data;
                const char* end = data + len;
                const char* crlf = nullptr;
                for (const char* q = p; q + 1 < end; ++q) {
                    if (q[0] == '\r' && q[1] == '\n') { crlf = q; break; }
                }
                if (crlf) {
                    std::string size_hex(p, crlf);
                    auto semi = size_hex.find(';');
                    if (semi != std::string::npos) size_hex = size_hex.substr(0, semi);
                    size_hex = TrimSpaces(size_hex);

                    size_t chunk_size = 0;
                    std::istringstream iss(size_hex);
                    iss >> std::hex >> chunk_size;
                    if (iss && !iss.fail()) {
                        size_t cursor = (size_t)(crlf - data) + 2;
                        size_t need = (chunk_size == 0) ? (cursor + 2) : (cursor + chunk_size + 2);
                        if (need <= len) {
                            if (chunk_size == 0 || (data[cursor + chunk_size] == '\r' && data[cursor + chunk_size + 1] == '\n')) {
                                size_t total = need;
                                if (len > total) { st.extra_tail = len - total; if (extra_tail_len) *extra_tail_len = st.extra_tail; }
                                st.is_chunked = true;
                                st.message_complete = true;
                                st.need_more = 0;
                                return st;
                            }
                        }
                        else { st.need_more = need - len; return st; }
                    }
                }
                // 否则判定为“头未完整”，返回保守 need_more
                st.need_more = 1;
                if (len >= 3 && s.substr(len - 3) == "\r\n\r") st.need_more = 1;
                else if (len >= 2 && s.substr(len - 2) == "\r\n") st.need_more = 2;
                else if (len >= 1 && s.back() == '\r') st.need_more = 3;
                return st;
            }

            // 头已完整
            st.header_complete = true;
            st.header_end = hdrEnd;
            std::string headers = s.substr(0, hdrEnd);
            bool chunked = HeaderHasChunked(headers);
            st.is_chunked = chunked;

            if (chunked) {
                // 仅收到头：把头当作独立消息（上层可准备资源）
                if (len == hdrEnd) {
                    st.message_complete = true;
                    st.need_more = 0;
                    return st;
                }
                // multipart：用“chunked-wire 扫描器”提取一个 part
                std::string boundary = ExtractBoundaryFromHeaders(headers);
                if (!boundary.empty()) {
                    size_t wire_cons = 0;
                    if (!ScanMultipartPartInChunkedWire(data + hdrEnd, len - hdrEnd, boundary, wire_cons)) {
                        st.need_more = 1; // 需要更多线缆字节
                        return st;
                    }
                    size_t total = hdrEnd + wire_cons;
                    if (len > total) { st.extra_tail = len - total; if (extra_tail_len) *extra_tail_len = st.extra_tail; }
                    st.message_complete = true;
                    st.need_more = 0;
                    return st;
                }
                // 非 multipart 的 chunked：将首个 chunk 作为独立单元
                const char* body = data + hdrEnd;
                size_t body_len = len - hdrEnd;
                const char* p = body;
                const char* end = body + body_len;
                const char* crlf = nullptr;
                for (const char* q = p; q + 1 < end; ++q) {
                    if (q[0] == '\r' && q[1] == '\n') { crlf = q; break; }
                }
                if (!crlf) { st.need_more = 1; return st; }
                std::string size_hex(p, crlf);
                auto semi = size_hex.find(';'); if (semi != std::string::npos) size_hex = size_hex.substr(0, semi);
                size_hex = TrimSpaces(size_hex);
                size_t chunk_size = 0;
                { std::istringstream iss(size_hex); iss >> std::hex >> chunk_size; if (!iss || iss.fail()) { st.need_more = 1; return st; } }
                size_t cursor = (size_t)(crlf - body) + 2;
                if (chunk_size == 0) {
                    // 终止块 + trailer
                    while (true) {
                        if (cursor + 2 > body_len) { st.need_more = 1; return st; }
                        const char* lp = body + cursor;
                        const char* ln = nullptr;
                        for (const char* q = lp; q + 1 < end; ++q) { if (q[0] == '\r' && q[1] == '\n') { ln = q; break; } }
                        if (!ln) { st.need_more = 1; return st; }
                        if (ln == lp) { cursor += 2; break; }
                        cursor = (size_t)(ln - body) + 2;
                    }
                    size_t total = hdrEnd + cursor;
                    if (len > total) { st.extra_tail = len - total; if (extra_tail_len) *extra_tail_len = st.extra_tail; }
                    st.message_complete = true;
                    st.need_more = 0;
                    return st;
                }
                if (cursor + chunk_size + 2 > body_len) { st.need_more = (cursor + chunk_size + 2) - body_len; return st; }
                if (body[cursor + chunk_size] != '\r' || body[cursor + chunk_size + 1] != '\n') { st.need_more = 1; return st; }
                size_t total = hdrEnd + cursor + chunk_size + 2;
                if (len > total) { st.extra_tail = len - total; if (extra_tail_len) *extra_tail_len = st.extra_tail; }
                st.message_complete = true;
                st.need_more = 0;
                return st;
            }

            // 非 chunked：依据 Content-Length（支持普通 GET/POST；若 Content-Type=multipart，则用实体扫描器抽一个 part）
            long cl = ParseContentLength(headers);
            if (cl < 0) {
                // 无载体：头完整即消息完整
                st.message_complete = true;
                st.need_more = 0;
                if (len > hdrEnd) { st.extra_tail = len - hdrEnd; if (extra_tail_len) *extra_tail_len = st.extra_tail; }
                return st;
            }
            size_t need = hdrEnd + (size_t)cl;
            if (len < need) { st.need_more = need - len; return st; }
            // 有完整实体：若为 multipart，则按 boundary 抽一个 part
            std::string boundary = ExtractBoundaryFromHeaders(headers);
            if (!boundary.empty()) {
                size_t consumedPart = 0;
                bool ok = ScanOneMultipartPart(data + hdrEnd, (size_t)cl, boundary, consumedPart);
                if (ok) {
                    size_t total = hdrEnd + consumedPart;
                    // 把本 part 当作一条独立消息（剩余实体与后续请求都作为 extra_tail）
                    if (len > total) { st.extra_tail = len - total; if (extra_tail_len) *extra_tail_len = st.extra_tail; }
                    st.message_complete = true;
                    st.need_more = 0;
                    return st;
                }
                // 若没找到完整 part，则把整个实体当作完整（兼容常规处理）
            }
            // 常规：整条消息完整 / 粘包
            if (len > need) { st.extra_tail = len - need; if (extra_tail_len) *extra_tail_len = st.extra_tail; }
            st.message_complete = true;
            st.need_more = 0;
            return st;
        }
        // 其余已有工具函数...
        inline bool IsApiAccept(const std::string& acc) {
            if (acc.empty()) return false;
            // 解析逗号分隔的媒体类型，提取 q 权重
            struct Item { std::string type; double q = 1.0; };
            std::vector<Item> items;
            std::string s = acc;
            std::transform(s.begin(), s.end(), s.begin(), ::tolower);
            std::stringstream ss(s);
            std::string part;
            while (std::getline(ss, part, ',')) {
                part = TrimSpaces(part);
                if (part.empty()) continue;
                Item it;
                // 拆分参数
                size_t sc = part.find(';');
                std::string mime = sc == std::string::npos ? part : TrimSpaces(part.substr(0, sc));
                it.type = mime;
                it.q = 1.0;
                while (sc != std::string::npos) {
                    size_t next = part.find(';', sc + 1);
                    std::string param = TrimSpaces(part.substr(sc + 1, (next == std::string::npos ? part.size() : next) - (sc + 1)));
                    sc = next;
                    if (param.rfind("q=", 0) == 0) {
                        try { it.q = std::stod(param.substr(2)); } catch (...) { /* ignore */ }
                    }
                }
                items.push_back(it);
            }
            auto bestQ = [&](const char* prefix)->double {
                double q = -1.0;
                for (auto& it : items) {
                    if (it.type == prefix || (std::strncmp(prefix, "application/", 12) == 0 && it.type.rfind("application/", 0) == 0) ) {
                        q = std::max(q, it.q);
                    }
                }
                return q;
            };
            double q_json = std::max(bestQ("application/json"), bestQ("text/json"));
            double q_html = bestQ("text/html");
            double q_any  = bestQ("*/*");

            // 判定策略：
            // 1) 明确接受 JSON（q_json >= 0）且其权重不低于 HTML，则视为 API。
            // 2) 若仅有 */*，不视为 API（避免误判）。
            if (q_json >= 0 && (q_html < 0 || q_json >= q_html)) return true;
            return false;
        }

        // x-www-form-urlencoded 解码（保留现有行为：+ -> 空格，%HH -> 字节）
        inline std::string UrlDecode(const std::string& s) {
            std::string out;
            out.reserve(s.size());
            auto hexval = [](char c) -> int {
                if (c >= '0' && c <= '9') return c - '0';
                if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
                if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
                return -1;
            };
            for (size_t i = 0; i < s.size(); ++i) {
                if (s[i] == '%') {
                    if (i + 2 < s.size()) {
                        int h = hexval(s[i + 1]), l = hexval(s[i + 2]);
                        if (h >= 0 && l >= 0) { out.push_back(char((h << 4) | l)); i += 2; }
                        else out.push_back(s[i]);
                    }
                    else out.push_back(s[i]);
                }
                else if (s[i] == '+') out.push_back(' ');
                else out.push_back(s[i]);
            }
            return out;
        }

        // x-www-form-urlencoded 编码（保留现有行为：空格 -> '+', 其他不安全字符 -> %HH）
        inline std::string UrlEncode(const std::string& s) {
            std::string out;
            out.reserve(s.size() * 3);
            auto hexval = [](unsigned char c) -> char {
                return (c < 10) ? char('0' + c) : char('A' + (c - 10));
            };
            for (unsigned char c : s) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    out.push_back(char(c));
                }
                else if (c == ' ') {
                    out.push_back('+');
                }
                else {
                    out.push_back('%');
                    out.push_back(hexval((c >> 4) & 0xF));
                    out.push_back(hexval(c & 0xF));
                }
            }
            return out;
        }

        // RFC 3986 编码：空格编码为 %20（用于路径/查询的更通用形式）
        inline std::string UrlEncodeRFC3986(const std::string& s) {
            std::string out;
            out.reserve(s.size() * 3);
            auto hex = [](unsigned char c) -> char {
                return (c < 10) ? char('0' + c) : char('A' + (c - 10));
            };
            for (unsigned char c : s) {
                if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
                    out.push_back(char(c));
                }
                else {
                    out.push_back('%');
                    out.push_back(hex((c >> 4) & 0xF));
                    out.push_back(hex(c & 0xF));
                }
            }
            return out;
        }

        // 将 JSON 对象/数组序列化为字符串后进行 URL 编码（用于 API 传输）
        inline std::string JsonUrlEncode(const njson& j, bool rfc3986 = false, int indent = -1) {
            std::string s = indent >= 0 ? j.dump(indent) : j.dump();
            return rfc3986 ? UrlEncodeRFC3986(s) : UrlEncode(s);
        }

        inline NetLibrary::njson PathToJson(const std::string& path) {
            const std::string p = UrlDecode(path);
            std::vector<std::string> segs;
            segs.reserve(8);
            std::string cur;
            cur.reserve(p.size());
            for (size_t i = 0; i < p.size(); ++i) {
                const char ch = p[i];
                if (ch == '/') {
                    if (!cur.empty()) { segs.emplace_back(cur); cur.clear(); }
                }
                else {
                    cur.push_back(ch);
                }
            }
            if (!cur.empty()) segs.emplace_back(cur);

            NetLibrary::njson out = NetLibrary::njson::object();
            out["segments"] = NetLibrary::njson::array();
            for (auto& s : segs) { out["segments"].push_back(s); }

            auto tolower_eq = [](const std::string& a, const char* b) {
                if (a.size() != std::strlen(b)) return false;
                for (size_t i = 0; i < a.size(); ++i) {
                    if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i]))) return false;
                }
                return true;
            };

            if (!segs.empty() && tolower_eq(segs[0], "api")) {
                if (segs.size() >= 2) out["resource"] = segs[1];
                if (segs.size() >= 3) out["id"] = segs[2];
            }
            else {
                if (!segs.empty()) out["resource"] = segs[0];
                if (segs.size() >= 2) out["id"] = segs[1];
            }
            out["hint"] = "parsed from path";
            return out;
        }

        inline bool IsIPv4(const std::string& s) {
            std::regex ipv4Regex(R"(^(\d{1,3}\.){3}\d{1,3}$)");
            return std::regex_match(s, ipv4Regex);
        }

        inline bool IsIPv6(const std::string& s) {
            std::regex ipv6Regex(R"(^(([0-9a-fA-F]{1,4}:){7}[0-9a-fA-F]{1,4}|(::[0-9a-fA-F]{1,4}){1,7}|([0-9a-fA-F]{1,4}::[0-9a-fA-F]{1,4}){1,6}|([0-9a-fA-F]{1,4}:[0-9a-fA-F]{1,4}::[0-9a-fA-F]{1,4}){1,5}|([0-9a-fA-F]{1,4}(:[0-9a-fA-F]{1,4}){0,2}::[0-9a-fA-F]{1,4}){1,4}|([0-9a-fA-F]{1,4}(:[0-9a-fA-F]{1,4}){0,3}::[0-9a-fA-F]{1,4}){1,3}|([0-9a-fA-F]{1,4}(:[0-9a-fA-F]{1,4}){0,4}::[0-9a-fA-F]{1,4}){1,2}|([0-9a-fA-F]{1,4}(:[0-9a-fA-F]{1,4}){0,5}::)|(::([0-9a-fA-F]{1,4}:){0,6}[0-9a-fA-F]{1,4}))$)");
            return std::regex_match(s, ipv6Regex);
        }

        inline std::string ExtractBearerToken(const char* authorization) {
            if (!authorization) return {};
            std::string s = TrimSpaces(authorization);
            if (s.size() < 7) return {};
            const char* prefix = "bearer ";
            bool match = true;
            for (size_t i = 0; i < 7; ++i) {
                if (std::tolower(static_cast<unsigned char>(s[i])) != prefix[i]) { match = false; break; }
            }
            if (!match) return {};
            std::string token = TrimSpaces(s.substr(7));
            size_t end = token.find_first_of(" \t\r\n,");
            if (end != std::string::npos) token = token.substr(0, end);
            return token;
        }

        inline std::string MakeCookiePathStd(const char* base_dir, const char* dom, const char* sid) {
            namespace fs = std::filesystem;
            auto safe = [](std::string s) {
                static const char* invalid = "<>:\"/\\|?*";
                for (char& ch : s) {
                    unsigned char b = static_cast<unsigned char>(ch);
                    if (std::strchr(invalid, ch) || b < 32) ch = '_';
                }
                return s;
            };
            fs::path dir = fs::path(base_dir ? base_dir : "").lexically_normal();
            std::error_code ec;
            if (!dir.empty() && !fs::exists(dir, ec)) { fs::create_directories(dir, ec); }

            std::string sdom = safe(dom ? dom : "");
            std::string ssid = safe(sid ? sid : "");
            fs::path file = dir / (sdom + "_" + ssid + ".cookie");

#ifdef _WIN32
            // 返回本地编码（ACP）的窄字节，便于与传统窄字节 API/日志配合
            return file.string();
#else
            // POSIX 下默认使用 UTF-8
            return file.u8string();
#endif
        }
    }; // namespace Httplib
}; // namespace NetLibrary