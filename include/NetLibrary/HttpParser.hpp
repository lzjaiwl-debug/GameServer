#pragma once
namespace NetLibrary {
	namespace Httplib {
        // Parsed part for multipart/form-data
        struct MultipartPart {
            std::unordered_map<std::string, std::string> headers;
            std::string name;          // Content-Disposition name
            std::string filename;      // optional
            std::string content_type;  // optional
            std::vector<char> data;
        };

        // Parsed HTTP request (only fields useful for POST handling)
        struct HttpRequest {
			std::string method;// GET, POST, etc.
			std::string uri;// full URI
			std::string version;// HTTP version
			std::unordered_map<std::string, std::string> headers;// header fields
            std::string body; // raw body

            // convenience parsed forms
            std::unordered_map<std::string, std::string> form; // for application/x-www-form-urlencoded
            std::vector<MultipartPart> files;                  // for multipart/form-data
            std::unordered_map<std::string, std::string> trailers; // for HTTP/1.1 chunked trailers

            bool IsContentType(const char* s) const {
                auto it = headers.find("content-type");
                if (it == headers.end()) return false;
                std::string v = it->second;
                std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
                return v.find(s) != std::string::npos;
            }

            // 新增：基于 RequestFieldCode 的便捷访问
            const std::string& Get(RequestFieldCode code) const {
                static const std::string empty;
                auto it = headers.find(HttpProRequestField::ToString((int)code));
                return it == headers.end() ? empty : it->second;
            }
            void Set(RequestFieldCode code, const std::string& value) {
                headers[HttpProRequestField::ToString((int)code)] = value;
            }
            const char* operator[](const char* key)
            {
                auto it = headers.find(key);
                if (it == headers.end()) return nullptr;
				return it->second.c_str();
            }
            // 新增：const 版本，支持在 const HttpRequest 上调用
            const char* operator[](const char* key) const {
                auto it = headers.find(key);
                if (it == headers.end()) return nullptr;
                return it->second.c_str();
            }
            // 新增：支持 std::string_view 键的便捷重载
            const char* operator[](std::string_view key) const {
                auto it = headers.find(std::string(key));
                if (it == headers.end()) return nullptr;
                return it->second.c_str();
            }
        };

        // Parsed HTTP response (basic fields)
        struct HttpResponse {
            std::string version;
            int status_code = 0;
            std::string reason;
            std::unordered_map<std::string, std::string> headers;
            std::string body; // raw body
            std::unordered_map<std::string, std::string> trailers; // for HTTP/1.1 chunked trailers

            bool IsContentType(const char* s) const {
                auto it = headers.find("content-type");
                if (it == headers.end()) return false;
                std::string v = it->second;
                std::transform(v.begin(), v.end(), v.begin(), [](unsigned char c) { return std::tolower(c); });
                return v.find(s) != std::string::npos;
            }

            // 新增：基于 ResponseFieldCode 的便捷访问
            const std::string& Get(ResponseFieldCode code) const {
                static const std::string empty;
                auto it = headers.find(HttpProResponseField::ToString((int)code));
                return it == headers.end() ? empty : it->second;
            }
            void Set(ResponseFieldCode code, const std::string& value) {
                headers[HttpProResponseField::ToString((int)code)] = value;
            }
            // 状态码辅助（可与 httpProDef.h 的 StatusCode 结合）
            void SetStatus(StatusCode code, const std::string& reason_phrase = std::string()) {
                status_code = static_cast<int>(code);
                if (!reason_phrase.empty()) reason = reason_phrase;
                else {
                    auto s = HttpProStatus::ToString(status_code);
                    reason = s ? s : std::string();
                }
            }
        };

        // Streaming HTTP parser/builder for requests and responses
        class HttpParser {
        public:
            HttpParser() : headers_parsed(false), content_length(0), chunked(false) {}
            HttpParser(const char* str) : HttpParser() {
                Append(str, std::strlen(str));
			}
            // Append raw bytes received from socket (HTTP/1.x)
            void Append(const char* data, size_t len) {
                buffer.append(data, len);
                if (!headers_parsed) TryParseHeaders();
                if (headers_parsed) TryParseBody();
            }

            // HTTP/2 framing input (no HPACK). Use this when handling HTTP/2 connections.
            void AppendHttp2(const char* data, size_t len) {
                h2_buffer.insert(h2_buffer.end(), data, data + len);
                TryParseHttp2Preface();
                TryParseHttp2Frames();
            }

            // Returns true if a full message (request or response) has been parsed
            bool IsComplete() const { return ready_request || ready_response; }
            bool IsRequestComplete() const { return ready_request; }
            bool IsResponseComplete() const { return ready_response; }

            // HTTP/2 frames availability
            bool HasHttp2Frame() const { return !h2_frames.empty(); }

            struct Http2Frame {
                uint32_t length = 0; // 24-bit on wire
                uint8_t type = 0;
                uint8_t flags = 0;
                uint32_t stream_id = 0;
                std::vector<unsigned char> payload;
            };

            // Consume next HTTP/2 frame if available
            std::unique_ptr<Http2Frame> ConsumeHttp2Frame() {
                if (h2_frames.empty()) return nullptr;
                auto f = std::make_unique<Http2Frame>(std::move(h2_frames.front()));
                h2_frames.pop_front();
                return f;
            }

            // Consume parsed request. Returns nullptr if none ready.
            std::unique_ptr<HttpRequest> ConsumeRequest() {
                if (!ready_request) return nullptr;
                ready_request = false;
                auto r = std::move(request);
                request.reset();
                // remove consumed data from buffer (we keep tail if extra pipelined data exists)
                buffer.erase(0, buffer_pos);
                buffer_pos = 0;
                // reset parser state for next message
                ResetState();
                return r;
            }

            // Consume parsed response. Returns nullptr if none ready.
            std::unique_ptr<HttpResponse> ConsumeResponse() {
                if (!ready_response) return nullptr;
                ready_response = false;
                auto resp = std::move(response);
                response.reset();
                // remove consumed data from buffer (we keep tail if extra pipelined data exists)
                buffer.erase(0, buffer_pos);
                buffer_pos = 0;
                // reset parser state for next message
                ResetState();
                return resp;
            }

            // --------- Builders ---------
            // Build a raw HTTP/1.x request string from HttpRequest
            static std::string BuildRequest(const HttpRequest& req) {
                // Determine body (use existing body, or encode from form/files if available)
                std::string body = req.body;
                std::unordered_map<std::string, std::string> hdrs = req.headers; // copy to adjust

                auto lct = getHeaderLower(hdrs, "content-type");
                if (body.empty()) {
                    if (lct.find("application/x-www-form-urlencoded") != std::string::npos && !req.form.empty()) {
                        body = buildUrlEncoded(req.form);
                    }
                    else if (lct.find("multipart/form-data") != std::string::npos && (!req.form.empty() || !req.files.empty())) {
                        // ensure boundary
                        std::string boundary = extractBoundary(hdrs["content-type"]);
                        if (boundary.empty()) {
                            boundary = makeBoundary();
                            hdrs["content-type"] = "multipart/form-data; boundary=" + boundary;
                        }
                        body = buildMultipart(boundary, req.form, req.files);
                    }
                }

                bool use_chunked = false;
                auto te = getHeaderLower(hdrs, "transfer-encoding");
                if (!te.empty()) use_chunked = (te.find("chunked") != std::string::npos);

                if (!use_chunked) {
                    hdrs["content-length"] = std::to_string(body.size());
                    // remove TE if present
                    auto it = hdrs.find("transfer-encoding"); if (it != hdrs.end()) hdrs.erase(it);
                }
                else {
                    if (!req.trailers.empty()) {
                        hdrs["trailer"] = joinTrailerNames(req.trailers);
                    }
                }

                std::ostringstream out;
                std::string version = req.version.empty() ? std::string("HTTP/1.1") : req.version;
                out << (req.method.empty() ? "GET" : req.method) << ' ' << (req.uri.empty() ? "/" : req.uri) << ' ' << version << "\r\n";
                for (const auto& kv : hdrs) {
                    out << kv.first << ": " << kv.second << "\r\n";
                }
                out << "\r\n";
                if (use_chunked) out << encodeChunked(body, req.trailers); else out << body;
                return out.str();
            }

            // Build a raw HTTP/1.x response string from HttpResponse
            static std::string BuildResponse(const HttpResponse& resp) {
                std::string body = resp.body;
                std::unordered_map<std::string, std::string> hdrs = resp.headers; // copy to adjust

                bool use_chunked = false;
                auto te = getHeaderLower(hdrs, "transfer-encoding");
                if (!te.empty()) use_chunked = (te.find("chunked") != std::string::npos);

                if (!use_chunked) {
                    hdrs["content-length"] = std::to_string(body.size());
                    auto it = hdrs.find("transfer-encoding"); if (it != hdrs.end()) hdrs.erase(it);
                }
                else {
                    if (!resp.trailers.empty()) {
                        hdrs["trailer"] = joinTrailerNames(resp.trailers);
                    }
                }

                std::ostringstream out;
                std::string version = resp.version.empty() ? std::string("HTTP/1.1") : resp.version;
                int code = resp.status_code == 0 ? 200 : resp.status_code;
                std::string reason = resp.reason.empty() ? std::string("OK") : resp.reason;
                out << version << ' ' << code << ' ' << reason << "\r\n";
                for (const auto& kv : hdrs) {
                    out << kv.first << ": " << kv.second << "\r\n";
                }
                out << "\r\n";
                if (use_chunked) out << encodeChunked(body, resp.trailers); else out << body;
                return out.str();
            }

            // Build HTTP/2 frame (payload should already be HPACK-encoded for HEADERS)
            static std::string BuildHttp2Frame(uint8_t type, uint8_t flags, uint32_t stream_id, const std::string& payload) {
                std::string out;
                out.resize(9);
                // length 24-bit
                uint32_t len = static_cast<uint32_t>(payload.size());
                out[0] = static_cast<char>((len >> 16) & 0xFF);
                out[1] = static_cast<char>((len >> 8) & 0xFF);
                out[2] = static_cast<char>((len) & 0xFF);
                out[3] = static_cast<char>(type);
                out[4] = static_cast<char>(flags);
                // stream id 31-bit
                uint32_t sid = stream_id & 0x7FFFFFFFu;
                out[5] = static_cast<char>((sid >> 24) & 0xFF);
                out[6] = static_cast<char>((sid >> 16) & 0xFF);
                out[7] = static_cast<char>((sid >> 8) & 0xFF);
                out[8] = static_cast<char>((sid) & 0xFF);
                out.append(payload);
                return out;
            }

            // Streaming multipart builder
            class MultipartStreamer {
            public:
                explicit MultipartStreamer(std::string boundary = makeBoundary()) : boundary_(std::move(boundary)) {}
                const std::string& Boundary() const { return boundary_; }

                // First call optional: not required to emit anything up-front
                std::string AddField(const std::string& name, const std::string& value) {
                    std::ostringstream os;
                    os << "--" << boundary_ << "\r\n";
                    os << "Content-Disposition: form-data; name=\"" << name << "\"\r\n\r\n";
                    os << value << "\r\n";
                    return os.str();
                }

                std::string BeginFile(const std::string& name, const std::string& filename, const std::string& content_type = std::string(),
                    const std::unordered_map<std::string, std::string>& extra_headers = {}) {
                    std::ostringstream os;
                    os << "--" << boundary_ << "\r\n";
                    os << "Content-Disposition: form-data; name=\"" << name << "\"; filename=\"" << filename << "\"\r\n";
                    if (!content_type.empty()) os << "Content-Type: " << content_type << "\r\n";
                    for (const auto& kv : extra_headers) os << kv.first << ": " << kv.second << "\r\n";
                    os << "\r\n";
                    return os.str();
                }

                std::string AppendFileData(const char* data, size_t len) {
                    return std::string(data, data + len);
                }

                std::string EndFile() {
                    return std::string("\r\n", 2);
                }

                std::string Finish() {
                    std::ostringstream os;
                    os << "--" << boundary_ << "--\r\n";
                    return os.str();
                }
            private:
                std::string boundary_;
            };

        private:
            std::string buffer;
            size_t buffer_pos = 0; // how many bytes consumed for current message
            bool headers_parsed;
            size_t content_length;
            bool chunked;
            bool ready_request = false;
            bool ready_response = false;
            std::unique_ptr<HttpRequest> request;
            std::unique_ptr<HttpResponse> response;

            // HTTP/2 state
            std::vector<char> h2_buffer;
            std::deque<Http2Frame> h2_frames;
            bool h2_preface_consumed = false;

            enum class ParseMode { Unknown, Request, Response };
            ParseMode mode = ParseMode::Unknown;
        public:
            /// <summary>
            /// 去除字符串两端的空白并将所有字母转换为小写。
            /// </summary>
            /// <param name="s">要处理的输入字符串。函数在副本上进行裁剪和转换，原始字符串不变。</param>
            /// <returns>处理后的字符串：已去除两端空白并将字母转换为小写。如果输入仅包含空白字符，则返回空字符串。</returns>
            static inline std::string toLowerTrim(const std::string& s) {
                std::string r = s;
                // trim
                auto l = r.find_first_not_of(" \t\r\n");
                auto e = r.find_last_not_of(" \t\r\n");
                if (l == std::string::npos) return "";
                r = r.substr(l, e - l + 1);
                std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
                return r;
            }
            /// <summary>
            /// 去除字符串两端的空白字符。
            /// </summary>
            /// <param name="s">要处理的输入字符串。函数会移除前后出现的空格、制表符、回车和换行等空白字符。</param>
            /// <returns>返回一个新的字符串，包含原字符串去除前后空白后的内容。如果原字符串全由空白字符组成，返回空字符串。</returns>
            static inline std::string trim(const std::string& s) {
                auto l = s.find_first_not_of(" \t\r\n");
                auto e = s.find_last_not_of(" \t\r\n");
                if (l == std::string::npos) return "";
                return s.substr(l, e - l + 1);
            }
            /// <summary>
            /// 将字符串转换为小写形式。
            /// </summary>
            /// <param name="s">要转换为小写的输入字符串。</param>
            /// <returns>返回一个新字符串，包含输入字符串中所有字符转换为小写后的结果（作为原字符串的副本）。</returns>
            static inline std::string toLower(const std::string& s) {
                std::string r = s;
                std::transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return std::tolower(c); });
                return r;
            }
            /// <summary>
            /// 从给定的 header 映射中按键查找并返回对应的值（先尝试原键，若未找到则尝试键的小写形式）；返回值会转换为小写并去除首尾空白，找不到时返回空字符串。
            /// </summary>
            /// <param name="hdrs">包含 header 名称到值的映射（std::unordered_map<std::string, std::string>），在其中查找指定键。</param>
            /// <param name="key">要查找的 header 名称；如果以原始形式未找到，会尝试该键的小写形式。</param>
            /// <returns>找到的 header 值，已转换为小写并去除首尾空白；如果未找到则返回空字符串。</returns>
            static inline std::string getHeaderLower(const std::unordered_map<std::string, std::string>& hdrs, const std::string& key) {
                auto it = hdrs.find(key);
                if (it != hdrs.end()) return toLowerTrim(it->second);
                // try lower-case key too
                std::string lk = toLower(key);
                auto it2 = hdrs.find(lk);
                if (it2 != hdrs.end()) return toLowerTrim(it2->second);
                return {};
            }
            /// <summary>
            /// 将无序映射（trailers）中的键（名称）连接成以逗号和空格分隔的字符串。
            /// </summary>
            /// <param name="trailers">从名称到值的无序映射。函数遍历映射的键并将它们连接为结果字符串。映射为空时返回空字符串。由于使用 unordered_map，键的遍历顺序未定义。</param>
            /// <returns>包含映射中所有键、以逗号和空格分隔的字符串（例如: a, b, c）。如果映射没有元素则返回空字符串。</returns>
            static std::string joinTrailerNames(const std::unordered_map<std::string, std::string>& trailers) {
                std::ostringstream os;
                bool first = true;
                for (const auto& kv : trailers) {
                    if (!first) os << ", ";
                    first = false;
                    os << kv.first;
                }
                return os.str();
            }
        private:
            /// <summary>
            /// 重置解析器的内部状态。将 headers_parsed 置为 false、content_length 置为 0、chunked 置为 false，并将 mode 重置为 ParseMode::Unknown。
            /// </summary>
            void ResetState() {
                headers_parsed = false;
                content_length = 0;
                chunked = false;
                mode = ParseMode::Unknown;
            }
            /// <summary>
            /// 解析缓冲区中的 HTTP 起始行和首部，将其填充到内部的 HttpRequest 或 HttpResponse 对象，并根据首部确定消息体处理方式（Content-Length / Transfer-Encoding: chunked）。
            /// </summary>
            void TryParseHeaders() {
                // find header/body separator
                auto pos = buffer.find("\r\n\r\n");
                if (pos == std::string::npos) return;
                std::string header_block = buffer.substr(0, pos + 2); // keep last CRLF for easier processing
                std::istringstream hs(header_block);
                std::string line;
                if (!std::getline(hs, line)) return;
                if (!line.empty() && line.back() == '\r') line.pop_back();
                // parse start line (request or response)
                if (line.rfind("HTTP/", 0) == 0) {
                    mode = ParseMode::Response;
                    response = std::make_unique<HttpResponse>();
                    std::istringstream start(line);
                    start >> response->version >> response->status_code;
                    std::getline(start, response->reason);
                    if (!response->reason.empty() && response->reason.front() == ' ') response->reason.erase(0, 1);
                }
                else {
                    mode = ParseMode::Request;
                    std::istringstream start(line);
                    request = std::make_unique<HttpRequest>();
                    request->method = line.substr(0, line.find(' '));
                    request->uri = line.substr(line.find(' ') + 1, line.rfind(' ') - line.find(' ') - 1);
                    request->version = line.substr(line.rfind(' ') + 1);
                }
                // parse headers
                while (std::getline(hs, line)) {
                    if (!line.empty() && line.back() == '\r') line.pop_back();
                    if (line.empty()) break;
                    auto colon = line.find(':');
                    if (colon == std::string::npos) continue;
                    std::string name = toLowerTrim(line.substr(0, colon));
                    std::string value = trim(line.substr(colon + 1));
                    if (mode == ParseMode::Request) {
                        request->headers[name] = value;
                    }
                    else if (mode == ParseMode::Response) {
                        response->headers[name] = value;
                    }
                }
                headers_parsed = true;
                buffer_pos = pos + 4; // body starts after CRLFCRLF
                // determine body handling
                const std::unordered_map<std::string, std::string>* hdrs = nullptr;
                if (mode == ParseMode::Request) hdrs = &request->headers; else hdrs = &response->headers;
                auto it = hdrs->find("content-length");
                if (it != hdrs->end()) {
                    content_length = std::stoull(it->second);
                    chunked = false;
                }
                else {
                    // chunked?
                    auto it2 = hdrs->find("transfer-encoding");
                    if (it2 != hdrs->end()) {
                        std::string v = toLowerTrim(it2->second);
                        chunked = (v.find("chunked") != std::string::npos);
                    }
                    else {
                        content_length = 0; // no body or unknown length (close-delimited not handled)
                        chunked = false;
                    }
                }
                // Special cases for responses with no body (1xx, 204, 304)
                if (mode == ParseMode::Response && !chunked && content_length == 0) {
                    int sc = response->status_code;
                    if ((sc >= 100 && sc < 200) || sc == 204 || sc == 304) {
                        // finalize immediately
                        FinalizeMessage();
                    }
                }
                // attempt parse body immediately
            }
            /// <summary>
            /// 从内部缓冲区解析 HTTP 消息体。函数支持 chunked 传输编码和基于 Content-Length 的正文：解码分块数据、解析并保存可选的尾部（trailer）头，将正文赋值到 request 或 response，并在成功解析后调用 FinalizeMessage()。在数据不足或消息已就绪时会提前返回。
            /// </summary>
            void TryParseBody() {
                if (!headers_parsed) return;
                if (mode == ParseMode::Response && ready_response) return; // already finalized by header-only case
                if (mode == ParseMode::Request && ready_request) return;
                if (chunked) {
                    // decode chunked data into body; keep tail for remaining streams
                    size_t idx = buffer_pos;
                    std::string decoded;
                    while (true) {
                        // read chunk-size line
                        auto crlf = buffer.find("\r\n", idx);
                        if (crlf == std::string::npos) return; // need more data
                        std::string size_line = buffer.substr(idx, crlf - idx);
                        // hex size may have extensions; extract until ';'
                        auto semi = size_line.find(';');
                        if (semi != std::string::npos) size_line = size_line.substr(0, semi);
                        size_t chunk_size = 0;
                        std::istringstream ss(size_line);
                        ss >> std::hex >> chunk_size;
                        // move to chunk data start
                        idx = crlf + 2;
                        if (buffer.size() < idx + chunk_size + 2) return; // wait for full chunk plus CRLF
                        if (chunk_size > 0) decoded.append(buffer.data() + idx, chunk_size);
                        idx += chunk_size;
                        // expect CRLF after chunk
                        if (buffer.substr(idx, 2) != "\r\n") return; // malformed or incomplete
                        idx += 2;
                        if (chunk_size == 0) {
                            // parse optional trailer headers until CRLFCRLF
                            size_t tcur = idx;
                            while (true) {
                                auto eol = buffer.find("\r\n", tcur);
                                if (eol == std::string::npos) return; // wait more
                                if (eol == tcur) { // empty line -> end of trailers
                                    tcur += 2;
                                    break;
                                }
                                std::string line = buffer.substr(tcur, eol - tcur);
                                auto colon = line.find(':');
                                if (colon != std::string::npos) {
                                    std::string name = toLowerTrim(line.substr(0, colon));
                                    std::string value = trim(line.substr(colon + 1));
                                    if (mode == ParseMode::Request) request->trailers[name] = value; else response->trailers[name] = value;
                                }
                                tcur = eol + 2;
                            }
                            // end of chunks
                            if (mode == ParseMode::Request) {
                                request->body = std::move(decoded);
                            }
                            else {
                                response->body = std::move(decoded);
                            }
                            buffer_pos = tcur; // consume up to end of trailers
                            FinalizeMessage();
                            return;
                        }
                    }
                }
                else {
                    if (buffer.size() < buffer_pos + content_length) return;
                    if (mode == ParseMode::Request) {
                        request->body.assign(buffer.data() + buffer_pos, content_length);
                    }
                    else {
                        response->body.assign(buffer.data() + buffer_pos, content_length);
                    }
                    buffer_pos += content_length;
                    FinalizeMessage();
                }
            }
        public:
            /// <summary>
            /// 对百分号编码的字符串进行解码：将 %XX 形式的十六进制转义（十六进制字母不区分大小写）还原为对应字符，并将 '+' 转换为空格，其他字符保持不变。
            /// </summary>
            /// <param name="s">要解码的输入字符串，可能包含 %HH（十六进制）编码或 '+' 表示空格。</param>
            /// <returns>解码后的字符串（std::string）。</returns>
            static inline std::string percentDecode(const std::string& s) {
                std::string out;
                out.reserve(s.size());
                for (size_t i = 0; i < s.size(); ++i) {
                    if (s[i] == '%' && i + 2 < s.size()) {
                        int hi = std::isdigit((unsigned char)s[i + 1]) ? s[i + 1] - '0' : (std::tolower(s[i + 1]) - 'a' + 10);
                        int lo = std::isdigit((unsigned char)s[i + 2]) ? s[i + 2] - '0' : (std::tolower(s[i + 2]) - 'a' + 10);
                        char c = static_cast<char>((hi << 4) | lo);
                        out.push_back(c);
                        i += 2;
                    }
                    else if (s[i] == '+') out.push_back(' ');
                    else out.push_back(s[i]);
                }
                return out;
            }
            /// <summary>
            /// 将输入字符串进行百分号编码（URL 编码）：保留字母数字及 - _ . ~，空格替换为 '+'，其余字节以大写十六进制形式进行 %XX 编码。
            /// </summary>
            /// <param name="s">要编码的输入字符串；按字节处理（内部将字符转换为 unsigned char 以避免未定义行为）。</param>
            /// <returns>返回经过百分号编码的字符串。</returns>
            static inline std::string percentEncode(const std::string& s) {
                static const char* hex = "0123456789ABCDEF";
                std::string out;
                for (unsigned char c : s) {
                    if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') out.push_back(c);
                    else if (c == ' ') out.push_back('+');
                    else { out.push_back('%'); out.push_back(hex[c >> 4]); out.push_back(hex[c & 0xF]); }
                }
                return out;
            }
            // 统一解析 API 请求为一个标准化的 JSON 对象，主要输出字段：
            //   - api: 资源名（如 users、user 等）
            //   - id: 资源 id（仅当路径段看起来像数字或 UUID 时才设置）
            //   - action: 动作/子资源（如 login、logout、search 等，当路径段不像数字/UUID 时设置）
            //   - version: 如果路径包含版本号（如 v1 或 v1.2）则返回该字段
            //   - query: 解析后的查询参数对象（重复参数会被解析为数组）
            //   - path_segments: 路径分段数组（按 "/" 分割并解码）
            //   - sub_segments: api 段之后的所有剩余路径段数组
            //   - path_map: 将相邻成对的路径段映射为键值（例如 /users/123/posts/456 -> {"users":"123","posts":"456"}）
            // 支持的输入形式示例与输出说明：
            //   1) 路径式（常见 REST 路由，id 为数字）
            //      输入:  "/api/user/16"
            //      输出: { "api":"user", "id":"16", "query":{}, "path_segments":["api","user","16"],
            //              "sub_segments":["16"], "path_map":{"api":"user"} }
            //   2) 路径式（动作路由，非数字段 → action）
            //      输入:  "/api/user/login"
            //      输出: { "api":"user", "action":"login", "query":{}, "path_segments":["api","user","login"],
            //              "sub_segments":["login"], "path_map":{"api":"user"} }
            //   3) 路径式带版本与多段
            //      输入:  "/api/v1/users/123/posts/456?sort=desc&tag=1&tag=2"
            //      输出: { "version":"v1", "api":"users", "id":"123",
            //              "query":{"sort":"desc","tag":["1","2"]},
            //              "path_segments":["api","v1","users","123","posts","456"],
            //              "sub_segments":["123","posts","456"],
            //              "path_map":{"api":"v1","users":"123","posts":"456"} }
            //   4) 路径 + 查询参数
            //      输入:  "/api/user?type=login"
            //      输出: { "api":"user", "query":{"type":"login"}, "path_segments":["api","user"],
            //              "sub_segments":[] }
            //   5) 查询式（路径 + 查询参数）
            //      输入:  "/user?id=1&xx=2"
            //      输出: { "api":"user", "query":{"id":"1","xx":"2"}, "path_segments":["user"], "sub_segments":[] }
            //   6) 纯表单/查询字符串（常见 POST form 或直接 query）
            //      输入:  "api=user&id=1&xx=2"
            //      输出: { "api":"user", "query":{"api":"user","id":"1","xx":"2"}, "path_segments":[] }
            //   7) JSON：若输入是合法 JSON 且包含 api 或 query 字段，则直接返回解析后的 JSON 对象
            // 说明：
            //   - id 与 action 的区分：路径段全为数字或符合 UUID 格式（含连字符的十六进制）时视为 id，否则视为 action
            //   - path_map 采用邻段成对映射，适用于 /resource/{id}/sub/{sid} 风格
            static inline NetLibrary::njson ApiRequestToJson(const std::string& input, const std::string& apiname = "api") {
                // 使用内部 percentDecode 对输入进行解码，兼容百分号编码
                const std::string s = percentDecode(input);

                NetLibrary::njson out = NetLibrary::njson::object();
                out[apiname] = "";
                out["query"] = NetLibrary::njson::object();
                out["path_segments"] = NetLibrary::njson::array();
                out["path_map"] = NetLibrary::njson::object();

                // helper: is version token like v1 or v1.2
                auto is_version_token = [](const std::string& t) {
                    if (t.size() < 2 || (t[0] != 'v' && t[0] != 'V')) return false;
                    for (size_t i = 1; i < t.size(); ++i) if (!std::isdigit((unsigned char)t[i]) && t[i] != '.') return false;
                    return true;
                };

                // helper: 判断字符串是否看起来像 id（纯数字，或 UUID 格式）
                auto is_id_like = [](const std::string& t) {
                    if (t.empty()) return false;
                    // 纯数字
                    bool all_digit = true;
                    for (char c : t) { if (!std::isdigit((unsigned char)c)) { all_digit = false; break; } }
                    if (all_digit) return true;
                    // UUID 格式: xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx (36 字符)
                    if (t.size() == 36 && t[8] == '-' && t[13] == '-' && t[18] == '-' && t[23] == '-') {
                        bool valid = true;
                        for (size_t i = 0; i < t.size() && valid; ++i) {
                            if (i == 8 || i == 13 || i == 18 || i == 23) continue;
                            if (!std::isxdigit((unsigned char)t[i])) valid = false;
                        }
                        if (valid) return true;
                    }
                    return false;
                };

                // 先判断是否形如路径 (/...)
                if (!s.empty() && s.front() == '/') {
                    // 分离路径与查询串
                    const auto qm = s.find('?');
                    const std::string path = qm == std::string::npos ? s : s.substr(0, qm);
                    const std::string qstr = qm == std::string::npos ? "" : s.substr(qm + 1);

                    // 拆段并解码每段
                    std::vector<std::string> segs;
                    segs.reserve(8);
                    std::string cur;
                    cur.reserve(path.size());
                    for (size_t i = 0; i < path.size(); ++i) {
                        const char ch = path[i];
                        if (ch == '/') {
                            if (!cur.empty()) { segs.emplace_back(percentDecode(cur)); cur.clear(); }
                        }
                        else {
                            cur.push_back(ch);
                        }
                    }
                    if (!cur.empty()) segs.emplace_back(percentDecode(cur));

                    // 填充 path_segments
                    for (const auto& seg : segs)
                        out["path_segments"].push_back(seg);

                    // 识别版本与资源
                    size_t idx = 0;
                    std::string version;
                    std::string apinameUpper = apiname;
                    std::transform(apinameUpper.begin(), apinameUpper.end(), apinameUpper.begin(),
                        [](unsigned char c) { return std::toupper(c); });
                    if (!segs.empty() && (segs[0] == apiname || segs[0] == apinameUpper)) {
                        idx = 1; // after 'api'
                        if (idx < segs.size() && is_version_token(segs[idx])) { version = segs[idx]; idx++; }
                    }
                    else if (!segs.empty() && is_version_token(segs[0])) {
                        version = segs[0]; idx = 1;
                    }

                    if (idx < segs.size()) out[apiname] = segs[idx];
                    if (!version.empty()) out["version"] = version;

                    // 收集 api 段之后的所有剩余段到 sub_segments
                    NetLibrary::njson sub_segs = NetLibrary::njson::array();
                    for (size_t i = idx + 1; i < segs.size(); ++i)
                        sub_segs.push_back(segs[i]);
                    out["sub_segments"] = std::move(sub_segs);

                    // 判断紧跟 api 之后的段是 id 还是 action
                    if (idx + 1 < segs.size()) {
                        const std::string& next_seg = segs[idx + 1];
                        if (is_id_like(next_seg))
                            out["id"] = next_seg;
                        else
                            out["action"] = next_seg;
                    }

                    // 额外：按成对段构建 path_map（例如 /users/123/posts/456 -> {users:123, posts:456}）
                    for (size_t i = 0; i + 1 < segs.size(); i += 2) {
                        std::string key = segs[i];
                        std::string val = segs[i + 1];
                        if (!key.empty()) out["path_map"][key] = val;
                    }

                    // 解析查询串到 query（支持重复键变数组）
                    if (!qstr.empty()) {
                        size_t start = 0;
                        while (start < qstr.size()) {
                            size_t amp = qstr.find('&', start);
                            std::string pair = (amp == std::string::npos) ? qstr.substr(start)
                                : qstr.substr(start, amp - start);
                            if (!pair.empty()) {
                                size_t eq = pair.find('=');
                                std::string key = percentDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
                                std::string val = percentDecode(eq == std::string::npos ? "" : pair.substr(eq + 1));
                                if (!key.empty()) {
                                    auto it = out["query"].find(key);
                                    if (it == out["query"].end()) {
                                        out["query"][key] = val;
                                    }
                                    else {
                                        if (it->is_array()) it->push_back(val);
                                        else {
                                            NetLibrary::njson arr = NetLibrary::njson::array();
                                            arr.push_back(*it);
                                            arr.push_back(val);
                                            out["query"][key] = std::move(arr);
                                        }
                                    }
                                }
                            }
                            if (amp == std::string::npos) break;
                            start = amp + 1;
                        }
                    }
                    return out;
                }

                // 非以 / 开头：尝试解析为 JSON（常见于 POST application/json）
                try {
                    NetLibrary::njson j = NetLibrary::njson::parse(s);
                    // 若已有 api / query 字段，直接返回原 JSON
                    if (j.is_object() && (j.contains(apiname) || j.contains("query"))) return j;
                    // 否则继续按表单或键值进行提取
                }
                catch (...) {
                    // fallthrough to form parsing
                }

                // 视为表单串：支持 api=user&id=1&xx=2
                {
                    NetLibrary::njson query = NetLibrary::njson::object();
                    std::string apiName;
                    std::string idValue;

                    size_t start = 0;
                    while (start < s.size()) {
                        size_t amp = s.find('&', start);
                        std::string pair = (amp == std::string::npos) ? s.substr(start)
                            : s.substr(start, amp - start);
                        if (!pair.empty()) {
                            size_t eq = pair.find('=');
                            std::string key = percentDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
                            std::string val = percentDecode(eq == std::string::npos ? "" : pair.substr(eq + 1));
                            if (!key.empty()) {
                                if (key == apiname) apiName = val;
                                else if (key == "id") idValue = val;

                                auto it = query.find(key);
                                if (it == query.end()) {
                                    query[key] = val;
                                }
                                else {
                                    if (it->is_array()) it->push_back(val);
                                    else {
                                        NetLibrary::njson arr = NetLibrary::njson::array();
                                        arr.push_back(*it);
                                        arr.push_back(val);
                                        query[key] = std::move(arr);
                                    }
                                }
                            }
                        }
                        if (amp == std::string::npos) break;
                        start = amp + 1;
                    }

                    out[apiname] = apiName;
                    out["query"] = std::move(query);
                    if (!idValue.empty()) out["id"] = idValue;
                    return out;
                }
            }

            // 修改 JsonUrlDecode：在检测到路径或表单时，优先返回统一格式；否则保留原有解析。
            static inline NetLibrary::njson JsonUrlDecode(const std::string& encoded, const std::string& apiname = "api") {
                const std::string s = UrlDecode(encoded);

                // 路径或表单统一规范解析
                if ((!s.empty() && s.front() == '/') || s.find('=') != std::string::npos) {
                    return ApiRequestToJson(encoded, apiname);
                }
                try {
                    return NetLibrary::njson::parse(s);
                }
                catch (...) {
                    // 兼容旧行为：key=value&... -> 对象
                    NetLibrary::njson obj = NetLibrary::njson::object();
                    if (s.empty()) return obj;
                    size_t start = 0;
                    while (start < s.size()) {
                        size_t amp = s.find('&', start);
                        std::string pair = (amp == std::string::npos) ? s.substr(start)
                            : s.substr(start, amp - start);
                        if (!pair.empty()) {
                            size_t eq = pair.find('=');
                            std::string key = UrlDecode(eq == std::string::npos ? pair : pair.substr(0, eq));
                            std::string val = UrlDecode(eq == std::string::npos ? "" : pair.substr(eq + 1));
                            if (!key.empty()) {
                                auto it = obj.find(key);
                                if (it == obj.end()) {
                                    obj[key] = val;
                                }
                                else {
                                    if (it->is_array()) it->push_back(val);
                                    else {
                                        NetLibrary::njson arr = NetLibrary::njson::array();
                                        arr.push_back(*it);
                                        arr.push_back(val);
                                        obj[key] = std::move(arr);
                                    }
                                }
                            }
                        }
                        if (amp == std::string::npos) break;
                        start = amp + 1;
                    }
                    return obj;
                }
            }
            /// <summary>
            /// 根据给定的键值对构建 application/x-www-form-urlencoded 格式的字符串：对每个键和值调用 percentEncode，键与值用 '=' 连接，条目间用 '&' 分隔。
            /// </summary>
            /// <param name="form">包含表单字段的无序映射（键到值）。每个键和值都会被 percentEncode 编码。注意 unordered_map 的遍历顺序未定义，因此输出顺序不可预测；空映射将产生空字符串。</param>
            /// <returns>返回 URL 编码后的表单字符串（键值对以 '&' 分隔）；如果输入映射为空，则返回空字符串。</returns>
            static std::string buildUrlEncoded(const std::unordered_map<std::string, std::string>& form) {
                std::ostringstream os;
                bool first = true;
                for (const auto& kv : form) {
                    if (!first) os << '&';
                    first = false;
                    os << percentEncode(kv.first) << '=' << percentEncode(kv.second);
                }
                return os.str();
            }
            /// <summary>
            /// 从 Content-Type 字符串中提取 multipart 的 boundary 值（如果存在）。
            /// </summary>
            /// <param name="ct">表示 HTTP Content-Type 头的完整值，例如 "multipart/form-data; boundary=----WebKitFormBoundary..."。函数在该字符串中查找 "boundary=" 并返回其后的内容。</param>
            /// <returns>提取并返回 boundary 字符串（会去掉可选的包裹引号）；如果未找到 "boundary=" 则返回空字符串。</returns>
            static inline std::string extractBoundary(const std::string& ct) {
                // ct like: multipart/form-data; boundary=----WebKitFormBoundary...
                auto i = ct.find("boundary=");
                if (i == std::string::npos) return {};
                std::string b = ct.substr(i + 9);
                // strip optional quotes
                if (!b.empty() && b.front() == '"') {
                    if (b.size() >= 2 && b.back() == '"') b = b.substr(1, b.size() - 2);
                }
                return b;
            }
            /// <summary>
            /// 生成一个边界字符串，每次调用在前缀后包含递增的计数器，常用于构造多部分消息的分隔符。
            /// </summary>
            /// <returns>包含前缀 ----NetLibBoundary 和递增计数器的字符串，例如 ----NetLibBoundary1。</returns>
            static std::string makeBoundary() {
                static unsigned long counter = 0;
                ++counter;
                std::ostringstream os;
                os << "----NetLibBoundary" << counter;
                return os.str();
            }
            /// <summary>
            /// 构建符合 multipart/form-data 格式的请求体字符串，包含表单字段和文件部分，使用指定的 boundary 作为分界标记。
            /// </summary>
            /// <param name="boundary">分界符字符串（不包含函数前面添加的两个短横线），函数内部会在每个分界前加上 "--" 来分隔各部分。</param>
            /// <param name="form">表单字段的键值映射 (field name -> value)，每个条目作为单独的 form-data 字段写入请求体。</param>
            /// <param name="files">要上传的文件部分列表（std::vector<MultipartPart>）。每个 MultipartPart 应包含 name、filename、content_type、headers 和 data 等信息；函数会为每个文件部分写入 Content-Disposition（以及可选的 filename）、可选的 Content-Type、其它自定义头（会忽略传入的 Content-Disposition 与 Content-Type），然后写入文件数据和行结束符。</param>
            /// <returns>返回组装好的 multipart/form-data 请求体（std::string），使用 CRLF (\r\n) 作为行结束符，结果可能包含二进制数据和空字节。</returns>
            static std::string buildMultipart(const std::string& boundary,
                const std::unordered_map<std::string, std::string>& form,
                const std::vector<MultipartPart>& files) {
                std::string dashb = "--" + boundary;
                std::ostringstream os;
                // form fields
                for (const auto& kv : form) {
                    os << dashb << "\r\n";
                    os << "Content-Disposition: form-data; name=\"" << kv.first << "\"\r\n\r\n";
                    os << kv.second << "\r\n";
                }
                // files
                for (const auto& p : files) {
                    os << dashb << "\r\n";
                    os << "Content-Disposition: form-data; name=\"" << p.name << "\"";
                    if (!p.filename.empty()) os << "; filename=\"" << p.filename << "\"";
                    os << "\r\n";
                    if (!p.content_type.empty()) os << "Content-Type: " << p.content_type << "\r\n";
                    // extra headers
                    for (const auto& kvh : p.headers) {
                        if (toLower(kvh.first) == "content-disposition" || toLower(kvh.first) == "content-type") continue;
                        os << kvh.first << ": " << kvh.second << "\r\n";
                    }
                    os << "\r\n";
                    os.write(p.data.data(), static_cast<std::streamsize>(p.data.size()));
                    os << "\r\n";
                }
                os << dashb << "--\r\n";
                return os.str();
            }
            /// <summary>
            /// 将输入数据按 HTTP 分块传输（chunked）编码为单个数据块的字符串。
            /// </summary>
            /// <param name="data">要编码为单个分块的源数据。</param>
            /// <returns>编码后的字符串，包含十六进制的块长度行、CRLF、数据、CRLF，以及终止块 "0\r\n\r\n"。</returns>
            static std::string encodeChunked(const std::string& data) {
                std::ostringstream os;
                // single-chunk encode (valid)
                os << std::hex << std::uppercase << data.size() << "\r\n";
                os << data << "\r\n";
                os << "0\r\n\r\n";
                return os.str();
            }
            /// <summary>
            /// 将给定的数据编码为单个 HTTP chunked 传输块，并可附加尾部头字段。
            /// </summary>
            /// <param name="data">作为 chunk 负载的字符串数据（原始字节序列）。</param>
            /// <param name="trailers">尾部头字段的映射，每个键值对会作为 "Key: Value" 行追加到终止块之后。</param>
            /// <returns>返回已编码的 chunked 字符串：先写入大小（大写十六进制）及 CRLF (\r\n)，然后是数据及 CRLF，接着写入终止块 "0" 及 CRLF，随后按每个尾部头字段输出 "Key: Value\r\n"，并以一个额外的 CRLF 结束。</returns>
            static std::string encodeChunked(const std::string& data, const std::unordered_map<std::string, std::string>& trailers) {
                std::ostringstream os;
                os << std::hex << std::uppercase << data.size() << "\r\n";
                os << data << "\r\n";
                os << "0\r\n";
                for (const auto& kv : trailers) {
                    os << kv.first << ": " << kv.second << "\r\n";
                }
                os << "\r\n";
                return os.str();
            }
        private:
            /// <summary>
            /// 解析 application/x-www-form-urlencoded 格式的请求体，将键值对拆分并对键和值进行百分号解码，然后将结果插入到 request->form 中。
            /// </summary>
            void ParseUrlEncodedForm() {
                std::string body = request->body;
                size_t pos = 0;
                while (pos < body.size()) {
                    auto amp = body.find('&', pos);
                    std::string pair = body.substr(pos, (amp == std::string::npos ? body.size() : amp) - pos);
                    auto eq = pair.find('=');
                    if (eq != std::string::npos) {
                        std::string k = percentDecode(pair.substr(0, eq));
                        std::string v = percentDecode(pair.substr(eq + 1));
                        request->form.emplace(std::move(k), std::move(v));
                    }
                    else {
                        std::string k = percentDecode(pair);
                        request->form.emplace(std::move(k), std::string());
                    }
                    if (amp == std::string::npos) break;
                    pos = amp + 1;
                }
            }
            /// <summary>
            /// 解析 HTTP multipart/form-data 请求体。函数从 request->headers 中读取 Content-Type，提取 boundary，遍历请求体中的每个部分，解析每部分的头部（如 Content-Disposition、Content-Type）、提取 name 和 filename（从双引号中读取），并将带 filename 的部分作为文件加入 request->files，其他部分作为表单字段加入 request->form。函数会跳过格式不全或缺少必要信息的情况，并在必要时修剪 CRLF 和处理结束边界。
            /// </summary>
            void ParseMultipart() {
                auto it = request->headers.find("content-type");
                if (it == request->headers.end()) return;
                std::string ct = it->second;
                std::string boundary = extractBoundary(ct);
                if (boundary.empty()) return;
                std::string dashb = "--" + boundary;
                std::string closeb = dashb + "--";
                size_t pos = 0;
                // body may start with CRLF; ensure correct search
                while (true) {
                    auto bpos = request->body.find(dashb, pos);
                    if (bpos == std::string::npos) break;
                    bpos += dashb.size();
                    if (request->body.substr(bpos, 2) == "--") break; // final boundary
                    // skip optional CRLF after boundary
                    if (request->body.substr(bpos, 2) == "\r\n") bpos += 2;
                    // parse part headers
                    auto hdrend = request->body.find("\r\n\r\n", bpos);
                    if (hdrend == std::string::npos) break;
                    std::string header_block = request->body.substr(bpos, hdrend - bpos);
                    MultipartPart part;
                    std::istringstream hs(header_block);
                    std::string line;
                    while (std::getline(hs, line)) {
                        if (!line.empty() && line.back() == '\r') line.pop_back();
                        if (line.empty()) continue;
                        auto col = line.find(':');
                        if (col == std::string::npos) continue;
                        std::string name = toLowerTrim(line.substr(0, col));
                        std::string value = trim(line.substr(col + 1));
                        part.headers[name] = value;
                        if (name == "content-disposition") {
                            // parse name and filename
                            // e.g.: form-data; name="field1"; filename="a.txt"
                            std::string s = value;
                            std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
                            // simple parser: look for name="..." and filename="..."
                            auto npos = value.find("name=");
                            if (npos != std::string::npos) {
                                auto start = value.find('\"', npos);
                                if (start != std::string::npos) {
                                    auto end = value.find('\"', start + 1);
                                    if (end != std::string::npos) part.name = value.substr(start + 1, end - start - 1);
                                }
                            }
                            auto fpos = value.find("filename=");
                            if (fpos != std::string::npos) {
                                auto start = value.find('\"', fpos);
                                if (start != std::string::npos) {
                                    auto end = value.find('\"', start + 1);
                                    if (end != std::string::npos) part.filename = value.substr(start + 1, end - start - 1);
                                }
                            }
                        }
                        else if (name == "content-type") {
                            part.content_type = value;
                        }
                    }
                    // data starts after hdrend+4, until next boundary
                    size_t data_start = hdrend + 4;
                    auto nextb = request->body.find(dashb, data_start);
                    if (nextb == std::string::npos) break;
                    size_t data_end = nextb;
                    // trim trailing CRLF
                    if (data_end >= 2 && request->body.substr(data_end - 2, 2) == "\r\n") data_end -= 2;
                    part.data.assign(request->body.begin() + data_start, request->body.begin() + data_end);
                    // store
                    if (!part.filename.empty()) {
                        request->files.push_back(std::move(part));
                    }
                    else {
                        // treat as simple form field
                        std::string value(part.data.begin(), part.data.end());
                        request->form.emplace(part.name, value);
                    }
                    pos = nextb;
                }
            }

            void FinalizeMessage() {
                if (mode == ParseMode::Request) {
                    // parse body according to content-type if helpful
                    auto it = request->headers.find("content-type");
                    if (it != request->headers.end()) {
                        std::string ct = it->second;
                        std::string lct = toLowerTrim(ct);
                        if (lct.find("application/x-www-form-urlencoded") != std::string::npos) {
                            ParseUrlEncodedForm();
                        }
                        else if (lct.find("multipart/form-data") != std::string::npos) {
                            ParseMultipart();
                        }
                        // else: leave body raw (e.g., application/json)
                    }
                    ready_request = true;
                }
                else if (mode == ParseMode::Response) {
                    // leave body raw for response
                    ready_response = true;
                }
            }

            void TryParseHttp2Preface() {
                static const char preface[] = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
                if (!h2_preface_consumed) {
                    if (h2_buffer.size() < sizeof(preface) - 1) return;
                    // if preface matches, consume it; otherwise assume server side (no preface) and continue
                    if (std::equal(preface, preface + sizeof(preface) - 1, h2_buffer.begin())) {
                        h2_buffer.erase(h2_buffer.begin(), h2_buffer.begin() + (sizeof(preface) - 1));
                    }
                    h2_preface_consumed = true;
                }
            }

            void TryParseHttp2Frames() {
                // parse as many complete frames as available
                while (true) {
                    if (h2_buffer.size() < 9) return;
                    // read header
                    uint32_t len = ((static_cast<unsigned char>(h2_buffer[0]) << 16) |
                        (static_cast<unsigned char>(h2_buffer[1]) << 8) |
                        (static_cast<unsigned char>(h2_buffer[2])));
                    if (h2_buffer.size() < 9u + len) return;
                    Http2Frame f;
                    f.length = len;
                    f.type = static_cast<uint8_t>(h2_buffer[3]);
                    f.flags = static_cast<uint8_t>(h2_buffer[4]);
                    uint32_t sid = ((static_cast<unsigned char>(h2_buffer[5]) << 24) |
                        (static_cast<unsigned char>(h2_buffer[6]) << 16) |
                        (static_cast<unsigned char>(h2_buffer[7]) << 8) |
                        (static_cast<unsigned char>(h2_buffer[8])));
                    f.stream_id = sid & 0x7FFFFFFFu;
                    if (len) f.payload.assign(h2_buffer.begin() + 9, h2_buffer.begin() + 9 + len);
                    // erase from buffer
                    h2_buffer.erase(h2_buffer.begin(), h2_buffer.begin() + 9 + len);
                    h2_frames.push_back(std::move(f));
                }
            }
        };

	};
};