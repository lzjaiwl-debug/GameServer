#ifndef _NetLibrary_proHeader_h_
#define _NetLibrary_proHeader_h_
namespace NetLibrary {
struct ProHttp : ::Moveable<ProHttp>
{
    Value httphead;
    mutable String _cache;
    ProHttp() = default;
    ProHttp(const String& hdrs) { Parse(hdrs); }
    ProHttp(const ProHttp& p) { httphead = p.httphead; }
    ProHttp(const Value& v) { httphead = v; }

    // 允许任意方法（GET/POST/PUT/DELETE/HEAD/OPTIONS...），仅当尚未构造响应头时可设置请求行
    ProHttp& SetRequestHead(const char* method, const char* path, ValueMap args)
    {
        if (!httphead.IsNull() || !httphead["status"].IsNull())
            return *this;
        if (!method || !path) return *this;
        httphead.GetAdd("method") = method;
        httphead.GetAdd("path") = path;
        httphead.GetAdd("args") = args;
        httphead.GetAdd("version") = "HTTP/1.1";
        return *this;
    }

    // 设置响应起始行（支持任意状态码），仅当尚未构造请求头时可设置响应行
    ProHttp& SetResponseHead(Httplib::StatusCode code)
    {
        if (!httphead.IsNull() || !httphead["method"].IsNull())
            return *this;
        httphead.GetAdd("status") = (int)code;
        httphead.GetAdd("version") = "HTTP/1.1";
        // 可选：原因短语
        const char* rp = Httplib::HttpProStatus::ToString((int)code);
        if (rp) httphead.GetAdd("reason") = rp;
        return *this;
    }

    // 添加请求头字段（仅请求模式）
    ProHttp& SetProtocolBody(Httplib::RequestFieldCode key, const Value& val)
    {
        if (httphead.IsNull() || httphead["method"].IsNull() || !httphead["status"].IsNull())
            return *this;
		String lowerKey = ToLower(Httplib::HttpProRequestField::ToString((int)key));
        httphead.GetAdd(lowerKey) = val;
        return *this;
    }

    // 添加响应头字段（仅响应模式）
    ProHttp& SetProtocolBody(Httplib::ResponseFieldCode key, const Value& val)
    {
        if (httphead.IsNull() || !httphead["method"].IsNull() || httphead["status"].IsNull())
            return *this;
        if(key == Httplib::HttpProResponseField::Set_Cookie && !val.Is<ValueMap>())
			return *this; // Set-Cookie 字段必须为 ValueMap 以支持多 cookie
		String lowerKey = ToLower(Httplib::HttpProResponseField::ToString((int)key));
        httphead.GetAdd(lowerKey) = val;
        return *this;
    }

    // 批量添加头字段（请求或响应模式都支持）
    ProHttp& SetProtocolBody(const Value& json)
    {
        if (httphead.IsNull()) return *this;
        if (!json.Is<ValueMap>()) return *this;
        const ValueMap& m = json;
        for (int i = 0; i < m.GetCount(); i++)
            httphead.GetAdd(m.GetKey(i)) = m.GetValue(i);
        return *this;
    }
    ProHttp& SetProtocolBody(njson json)
    {
        if (httphead.IsNull()) return *this;
        if (json.is_null()) return *this;
		String s = json.dump(4);
        Value v = ParseJSON(s);
        return SetProtocolBody(v);
    }
    // 设置载体；若未显式设置 content-length，则按字符串大小填充
    ProHttp& SetPayload(const Value& val)
    {
        if (val.Is<String>())
            httphead.GetAdd("content-length") = val.Get<String>().GetCount();
        else if (val.Is<WString>())
            httphead.GetAdd("content-length") = val.Get<WString>().GetCount();
        httphead.GetAdd("payload") = val;
        return *this;
    }

    // 解析：支持通用请求/响应起始行、大小写不敏感头名；载体统一进入 payload
    bool Parse(const String& hdrs)
    {
        StringStream ss(hdrs);
        Json head;
        // 暂存 cookie/set-cookie 的 ValueMap，解析完后注入 httphead
        ValueMap cookie_map;
        ValueMap setcookie_map;
        bool has_cookie = false;
        bool has_setcookie = false;
        while (!ss.IsEof())
        {
            String line = ss.GetLine();
            if (line.IsEmpty()) break;

            // 请求行：<METHOD> <PATH> HTTP/X.Y
            if (line.Find("HTTP/") > -1 && (line.Find("GET") > -1 || line.Find("POST") > -1 || line.Find("PUT") > -1
                || line.Find("DELETE") > -1 || line.Find("HEAD") > -1 || line.Find("OPTIONS") > -1 || line.Find("PATCH") > -1))
            {
                int verpos = line.Find("HTTP/");
                String method = TrimBoth(line.Mid(0, line.Find(' ')));
                int begin = line.FindAfter(method);
                int pend = verpos;
                int qm = line.Find('?');
                String path = TrimBoth(line.Mid(begin, (qm > -1 ? qm : pend) - begin));
                head("method", method);
                head("path", path);
                head("version", TrimBoth(line.Mid(verpos, line.GetCount() - verpos)));
                if (qm > -1)
                {
                    Json ar;
                    Vector<String> args = ::Split(line.Mid(qm + 1, pend - (qm + 1)), '&');
                    for (auto& a : args)
                    {
                        if (a.Find('=') > -1) { ::Vector<String> kv = Split(a, '='); ar(kv[0], kv[1]); }
                    }
                    head("args", ar);
                }
                continue;
            }
            // 响应行：HTTP/X.Y <status> <reason?>
            if (line.StartsWith("HTTP/"))
            {
                int verpos = 0;
                int sp1 = line.Find(' ', 0);
                int sp2 = line.Find(' ', sp1 + 1);
                String version = TrimBoth(line.Mid(verpos, sp1 - verpos));
                String status = TrimBoth(line.Mid(sp1 + 1, (sp2 > -1 ? sp2 : line.GetCount()) - (sp1 + 1)));
                head("version", version);
                head("status", ::StrInt(status));
                if (sp2 > -1)
                {
                    String reason = TrimBoth(line.Mid(sp2 + 1));
                    if (!reason.IsEmpty()) head("reason", reason);
                }
                continue;
            }
            // 头字段
            int q = line.Find(':');
            if (q > -1)
            {
                String k = ToLower(TrimBoth(line.Mid(0, q)));
                String v = TrimBoth(line.Mid(q + 1));
                if (k == "content-length") head(k, StrInt(v));
                else if (k == "cookie" || k == "set-cookie")
                {
                    ValueMap& target = (k == "cookie") ? cookie_map : setcookie_map;
                    if (k == "cookie") has_cookie = true; else has_setcookie = true;
                    int void_idx = 0;
                    ::Vector<String> items = ::Split(v, ';');
                    for (auto& a : items)
                    {
                        String t = TrimBoth(a);
                        String ck, cv;
                        int eq = t.Find('=');
                        if (eq > -1)
                        {
                            ck = TrimBoth(t.Mid(0, eq));
                            cv = TrimBoth(t.Mid(eq + 1));
                        }
                        else
                        {
                            ck = Format("void%d", void_idx++);
                            cv = t;
                        }
                        int existing = target.Find(ck);
                        if (existing < 0)
                        {
                            target.Add(ck, cv);
                        }
                        else
                        {
                            Value old = target.GetValue(existing);
                            if (old.Is<ValueArray>())
                            {
                                ValueArray arr(old);
                                arr.Add(cv);
                                target.SetAt(existing, arr);
                            }
                            else
                            {
                                ValueArray arr;
                                arr.Add(old);
                                arr.Add(cv);
                                target.SetAt(existing, arr);
                            }
                        }
                    }
                }
                else head(k, v);
            }
        }
        // 完成头集合
        httphead = ParseJSON(~head);
        if (httphead.IsError() || httphead.IsNull())
            return false;
        // 注入 cookie/set-cookie（直接用 ValueMap，避免 Json 重复 key 丢失问题）
        if (has_cookie)
            httphead.GetAdd("cookie") = cookie_map;
        if (has_setcookie)
            httphead.GetAdd("set-cookie") = setcookie_map;

        // 载体统一填入 payload：
        // 1) Content-Length：取定长
        // 2) Transfer-Encoding: chunked：保留原始线缆载体（头后的剩余字节）进入 payload（上层可自行解码）
        int point = hdrs.FindAfter("\r\n\r\n");
        if (point > -1)
        {
            if (!httphead["content-length"].IsNull())
            {
                int num = httphead["content-length"].Is<int>() ? httphead["content-length"].Get<int>()
                    : StrInt(~httphead["content-length"]);
                httphead.GetAdd("payload") = hdrs.Mid(point, num);
            }
            else
            {
                // 若为 chunked 或未知长度，直接将头尾后的所有内容作为原始载体
                String te = ToLower(httphead["transfer-encoding"].IsNull() ? String() : ~httphead["transfer-encoding"]);
                if (te.Find("chunked") > -1)
                    httphead.GetAdd("payload") = hdrs.Mid(point); // 原始 chunked wire（未解码）
                else
                {
                    // 没有 CL/TE：也视为无载体或未知载体；若存在内容，全部写入 payload
                    String tail = hdrs.Mid(point);
                    if (!tail.IsEmpty())
                        httphead.GetAdd("payload") = tail;
                }
            }
        }
        return true;
    }

    // 构建仅头部（请求或响应）
    String buildhead()
    {
        String httpbuf;
        if (httphead.IsNull() || !httphead.Is<ValueMap>())
            return ::Nuller();
        const ValueMap& map = httphead;

        // 请求起始行
        if (!httphead["method"].IsNull())
        {
            String method = httphead["method"];
            String path = httphead["path"];
            String version = httphead["version"].IsNull() ? "HTTP/1.1" : httphead["version"];
            httpbuf << method << ' ';
            httpbuf << path;
            if (!httphead["args"].IsNull())
            {
                if (httphead["args"].Is<ValueMap>())
                {
                    httpbuf << '?';
                    const ValueMap& ag = httphead["args"];
                    for (int ii = 0; ii < ag.GetCount(); ii++)
                    {
                        httpbuf << ag.GetKey(ii) << '=' << ag.GetValue(ii);
                        if (ii < ag.GetCount() - 1) httpbuf << '&';
                    }
                }
                else
                    httpbuf << '?' << httphead["args"];
            }
            httpbuf << ' ' << version << "\r\n";
        }
        // 响应起始行
        else if (!httphead["status"].IsNull())
        {
            String version = httphead["version"].IsNull() ? "HTTP/1.1" : httphead["version"];
            int code = httphead["status"].Is<int>() ? httphead["status"].Get<int>() : StrInt(~httphead["status"]);
            const char* rp = Httplib::HttpProStatus::ToString(code);
            String reason = (!httphead["reason"].IsNull() ? String(httphead["reason"]) : (rp ? String(rp).Mid(String(rp).Find(' ') + 1) : String()));
            httpbuf << version << ' ' << code;
            if (!reason.IsEmpty()) httpbuf << ' ' << reason;
            httpbuf << "\r\n";
        }

        // 头字段（除 payload）
        for (int i = 0; i < map.GetCount(); i++)
        {
            String k = map.GetKey(i);
            if (k == "method" || k == "path" || k == "args" || k == "version" || k == "status" || k == "reason" || k == "payload")
                continue;
            if (k == "cookie")
            {
                httpbuf << k << ": ";
                if (map.GetValue(i).Is<ValueMap>())
                {
                    ValueMap ag = map.GetValue(i);
                    bool first = true;
                    for (int ii = 0; ii < ag.GetCount(); ii++)
                    {
                        String ck = ag.GetKey(ii).ToString();
                        Value cv = ag.GetValue(ii);
                        bool is_void = ck.Find("void") > -1;
                        if (IsValueArray(cv))
                        {
                            ValueArray arr(cv);
                            for (int jj = 0; jj < arr.GetCount(); jj++)
                            {
                                if (!first) httpbuf << "; ";
                                first = false;
                                if (is_void)
                                    httpbuf << arr[jj];
                                else
                                    httpbuf << ck << '=' << arr[jj];
                            }
                        }
                        else
                        {
                            if (!first) httpbuf << "; ";
                            first = false;
                            if (is_void)
                                httpbuf << cv;
                            else
                                httpbuf << ck << '=' << cv;
                        }
                    }
                }
                else
                    httpbuf << map.GetValue(i);
                httpbuf << "\r\n";
            }
            else if (k == "set-cookie")
            {
                if (map.GetValue(i).Is<ValueMap>())
                {
                    ValueMap ag = map.GetValue(i);
                    // Set-Cookie 属性名集合（RFC 6265）
                    auto is_cookie_attr = [](const String& s) -> bool {
                        String low = ToLower(s);
                        return low == "max-age" || low == "expires" || low == "path"
                            || low == "domain" || low == "secure" || low == "httponly"
                            || low == "samesite";
                    };
                    // 收集属性部分
                    String attrs;
                    for (int ii = 0; ii < ag.GetCount(); ii++)
                    {
                        String ak = ag.GetKey(ii).Get<String>();
                        if (is_cookie_attr(ak))
                        {
                            if (!attrs.IsEmpty()) attrs << "; ";
                            attrs << ak << '=' << ag.GetValue(ii);
                        }
                    }
                    // 每个非属性的 key=value 作为独立的 Set-Cookie 行输出
                    for (int ii = 0; ii < ag.GetCount(); ii++)
                    {
                        String ck = ag.GetKey(ii).Get<String>();
                        if (is_cookie_attr(ck)) continue;
                        if (ck.Find("void") > -1)
                        {
                            httpbuf << "set-cookie: " << ag.GetValue(ii);
                        }
                        else
                        {
                            httpbuf << "set-cookie: " << ck << '=' << ag.GetValue(ii);
                        }
                        if (!attrs.IsEmpty())
                            httpbuf << "; " << attrs;
                        httpbuf << "\r\n";
                    }
                }
                else
                {
                    httpbuf << k << ": " << map.GetValue(i) << "\r\n";
                }
            }
            else
                httpbuf << k << ": " << ParseValue(map.GetValue(i)) << "\r\n";
        }
        httpbuf << "\r\n";
        return httpbuf;
    }

    // 返回载体（若无则空）
    String get_payload()
    {
        if (httphead["payload"].IsNull())
            return Nuller();
        return httphead["payload"];
    }

    // 完整构建（头 + 载体）
    String build()
    {
        String httpbuf;
        return httpbuf << buildhead() << get_payload();
    }

    operator const char* () { _cache = buildhead(); return _cache; }
    const char* operator ~ () { _cache = buildhead(); return _cache; }
    const char* operator ++ () { _cache = build(); return _cache; }
    operator bool () { return !httphead.IsNull(); }
    ProHttp& operator = (const ProHttp& p) { httphead = p.httphead; return *this; }
    Value operator[](const char * key) { return httphead[ToLower(key)]; }
};
};
#endif
