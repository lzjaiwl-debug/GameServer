#include "../inc/include.h"

String NetLibrary::Protocol::buildHttpErr(const String& err)
{
	ProHttp httpval;
	httpval.SetResponseHead(Httplib::HttpProStatus::BadRequest_400);
	httpval.SetProtocolBody(Httplib::HttpProResponseField::Content_Type, "text/plain");
	httpval.SetProtocolBody(Httplib::HttpProResponseField::Content_Length, err.GetLength());
	httpval.SetProtocolBody(Httplib::HttpProResponseField::Connection, "close");
	httpval.SetPayload(err);
	return httpval.build();
}
String Protocol::buildSioPollErr(const String& err)
{
	ProHttp httpval;
	httpval.SetResponseHead(Httplib::HttpProStatus::BadRequest_400);
	httpval.SetProtocolBody(Httplib::HttpProResponseField::Content_Type,"application/json");
	httpval.SetProtocolBody(Httplib::HttpProResponseField::Content_Length,err.GetLength());
	httpval.SetProtocolBody(Httplib::HttpProResponseField::Connection,"close");
	httpval.SetPayload(err);
	return httpval.build();
}

Value Protocol::Parse_Handshake(const String& httpstr, const Httplib::HttpRecvStatus& status)
{
	ProHttp http = httpstr;
	if(http)
		return http.httphead;
	return ::Nuller();
}
String Protocol::GetSocketKey()
{
	String key;
	uint64 randomData[2];
	char randomBytes[16];
	::Random64(randomData, 2);
	memcpy(randomBytes, randomData, sizeof(randomBytes));
	key = Base64Encode(randomBytes, 16);
	return key;
}
String Protocol::buildHandshake(String pyload, String sid, String key, ProtocolType ptype,
                                bool ispoll, const char* method)
{
	ProHttp httpval;
	//String retweb;
	if(type == SockType::SERVER) {
		if(ptype == ProtocolType::SOCKETIO && ispoll)
			httpval.SetResponseHead(Httplib::HttpProStatus::OK_200);
		else
			httpval.SetResponseHead(Httplib::HttpProStatus::SwitchingProtocol_101);
		if(!ispoll) {
			byte sha1[20];
			SHA1(sha1, key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
			String Sec = Base64Encode((char*)sha1, 20);
			String upgrade_value = (ptype == ProtocolType::SOCKET) ? "socket" : "websocket";
			httpval.SetProtocolBody(Httplib::HttpProResponseField::Connection,"upgrade");
			httpval.SetProtocolBody(Httplib::HttpProResponseField::Upgrade, upgrade_value);
			httpval.SetProtocolBody(Httplib::HttpProResponseField::Sec_WebSocket_Accept,Sec);
		}
		else {
			httpval.SetProtocolBody(Httplib::HttpProResponseField::Connection,"keep-alive");
			httpval.SetProtocolBody(Httplib::HttpProResponseField::Content_Type,"text/plain; charset=UTF-8");
			httpval.SetProtocolBody(Httplib::HttpProResponseField::Set_Cookie, sid);
			if(!pyload.IsEmpty())
				httpval.SetProtocolBody(Httplib::HttpProResponseField::Content_Length, pyload.GetLength()).SetPayload(pyload);
		}
	}
	else {
		if(ptype == ProtocolType::SOCKETIO && ispoll) {
			if(sid.IsEmpty()) {
				httpval.SetRequestHead(method,"/socket.io/",{{"EIO","4"},{"transport","polling"}});
				//retweb << "GET /socket.io/?EIO=4&transport=polling HTTP/1.1\r\n";
			}
			else {
				httpval.SetRequestHead(method,"/socket.io/",{{"EIO","4"},{"transport","polling"},{"sid",sid}});
				//retweb << "POST /socket.io/?EIO=4&transport=polling&sid=" + sid + " HTTP/1.1\r\n";
			}
		}
		else if(ptype == ProtocolType::SOCKETIO && !ispoll) {
			httpval.SetRequestHead(method,"/socket.io/",{{"EIO","4"},{"transport","websocket"},{"sid",sid}});
			//retweb << "GET /socket.io/?EIO=4&transport=websocket&sid=" + sid + " HTTP/1.1\r\n";
		}
		if(ptype == ProtocolType::WEBSOCKET) {
			httpval.SetRequestHead(method,"/websocket",::Nuller());
			//retweb << "GET /chat HTTP/1.1\r\n";
		}
		if(ptype == ProtocolType::SOCKET) {
			httpval.SetRequestHead(method,"/socket",::Nuller());
			//retweb << "GET /socket HTTP/1.1\r\n";
		}
		if(!ispoll) {
			String upgrade_value = (ptype == ProtocolType::SOCKET) ? "socket" : "websocket";
			clientKey = GetSocketKey();
			httpval.SetProtocolBody(Httplib::HttpProRequestField::Connection,"upgrade");
			httpval.SetProtocolBody(Httplib::HttpProRequestField::Upgrade, upgrade_value);
			httpval.SetProtocolBody(Httplib::HttpProRequestField::Sec_WebSocket_Key, clientKey);
			httpval.SetProtocolBody(Httplib::HttpProRequestField::Sec_WebSocket_Version,"13");
		}
		else {
			httpval.SetProtocolBody(Httplib::HttpProRequestField::Connection,"keep-alive");
			//retweb << "Connection: keep-alive\r\n";
			if(!pyload.IsEmpty())
				httpval.SetProtocolBody(Httplib::HttpProRequestField::Content_Length,pyload.GetLength()).SetPayload(pyload);
				//retweb << Format("Content-Length: %d\r\n\r\n%s", pyload.GetLength(), pyload);
		}
	}

	return httpval.build();
}

HttpProtocol Protocol::Parse_poll(const Value& pro, ProtocolType ptype)
{
	int len = 0;
	if (type == SockType::SERVER) {
		if (pro["args"]["sid"].IsNull() && pro["method"].ToString() == "GET") {
			return { EventType::SOCKETIOOPEN, "", false };
		}
		if (!pro["args"]["sid"].IsNull() && pro["payload"].ToString() == "40" && pro["method"].ToString() == "POST") {
			return { EventType::SOCKETIOPOLL , buildHandshake("ok", pro["args"]["sid"], "", ProtocolType::SOCKETIO),ScanUuid(~pro["args"]["sid"]) };
		}
		if (!pro["args"]["sid"].IsNull() && pro["payload"].IsNull() && pro["method"].ToString() == "GET") {
			String json = sio_message(frame_type::frame_message, state_type::type_connect)
				.SetVal("sid", pro["POST"]["sid"]);
			String str;
			if (ptype == ProtocolType::SOCKETIO)
				str = buildHandshake("6", "", "", ProtocolType::SOCKETIO);
			else
				str = buildHandshake(json, pro["args"]["sid"], "", ProtocolType::SOCKETIO);
			return { EventType::SOCKETIOPOLL , str, ScanUuid(~pro["args"]["sid"]) };
		}
		if (!pro["args"]["sid"].IsNull() &&	pro["args"]["transport"].ToString() == "websocket") {
			return { EventType::SOCKETIOHANDSHAKE ,buildHandshake("", "", pro["sec-websocket-key"], ProtocolType::SOCKETIO, false) , ScanUuid(~pro["args"]["sid"]) };
		}
		return { EventType::ERROREVENT ,buildSioPollErr(sio_message(frame_type::frame_message, state_type::type_error)
			.SetVal("req", "polling")
			.SetVal("code", 5)
			.SetVal("message", "Unsupported protocol version")) ,ScanUuid(~pro["args"]["sid"]) };
	}
	else
	{
		if (pro["status"] == "101") {
			byte sha1[20];
			SHA1(sha1, clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
			String Sec = Base64Encode((char*)sha1, 20);
			if (Sec != pro["sec-websocket-accept"])
				return { EventType::ERROREVENT,buildHttpErr(""), Nuller()};
			//pack->wirte(buildHandshake("", sio_sessions[0], "", ProtocolType::SOCKETIO, false));
			//WhenEvent(EventType::SOCKETIOHANDSHAKE, pack, sio_sessions[0]);
			return { EventType::SOCKETIOHANDSHAKE ,buildHandshake("", sio_sessions[0], "", ProtocolType::SOCKETIO, false) , sio_sessions[0] };
		}
		if(pro["status"] == 200 && !pro["payload"].IsNull()) {
			sio_message sio_msg;
			bool isparse = sio_msg.parse_msgbuffer(pro["payload"].ToString());
			if (!isparse && pro["payload"].ToString() == "ok")
			{
				//pack->wirte(buildHandshake("", sio_sessions[0], "", ProtocolType::SOCKETIO));
				//WhenEvent(EventType::SOCKETIOPOLL, pack, sio_msg);
				return { EventType::SOCKETIOPOLL ,buildHandshake("", sio_sessions[0], "", ProtocolType::SOCKETIO) , sio_msg };
			}
			if(sio_msg._type == frame_type::frame_noop)
				return {};
			if(sio_msg._type == frame_type::frame_open) {
				//pack->wirte(buildHandshake("", sio_msg["sid"], "", ProtocolType::SOCKETIO, true, "POST"));
				//WhenEvent(EventType::SOCKETIOPOLL, pack, sio_msg);
				if(sio_sessions.GetCount() > 0)
					sio_sessions[0] = sio_msg["sid"].ToString();
				else
					sio_sessions.Add(sio_msg["sid"].ToString());
				return { EventType::SOCKETIOOPEN ,buildHandshake("", sio_msg["sid"], "", ProtocolType::SOCKETIO, true, "POST") ,sio_msg };
			}
			if (sio_msg._type == frame_type::frame_message && sio_msg._state == state_type::type_connect)
			{
				//pack->wirte(buildHandshake("", sio_sessions[0], "", ProtocolType::SOCKETIO, false));
				//WhenEvent(EventType::SOCKETIOPOLL, pack, sio_msg);
				return { EventType::SOCKETIOPOLL ,buildHandshake("", sio_sessions[0], "", ProtocolType::SOCKETIO, false) ,sio_msg };
			}
		}
	}
	return { EventType::ERROREVENT,buildHttpErr(""),Nuller()};
}
HttpProtocol NetLibrary::Protocol::Parse_HttpHeadPro(const String& httpstr, const Httplib::HttpRecvStatus& status, ProtocolType ptype)
{
	Value pro = Parse_Handshake(httpstr, status);
	if (pro.IsNull() || pro.IsError())
		return { EventType::ERROREVENT, buildHttpErr("Invalid HTTP request") ,Nuller() };
	else
	{
		if (type == SockType::SERVER) {
			if (pro["path"].ToString().Find("socket.io") != -1) {
				if (pro["args"]["EIO"].ToString() != "4") {
					return { EventType::ERROREVENT, buildSioPollErr(sio_message(frame_type::frame_message, state_type::type_error)
						.SetVal("req", "protocol")
						.SetVal("code", 5)
						.SetVal("message", "Unsupported protocol version,no v4")) ,Nuller() };
				}
				if (pro["args"]["transport"].ToString() != "polling" && pro["args"]["transport"].ToString() != "websocket")
				{
					return { EventType::ERROREVENT, buildSioPollErr(sio_message(frame_type::frame_message, state_type::type_error)
						.SetVal("req", "transport ")
						.SetVal("code", 0)
						.SetVal("message", "Transport unknown")) ,Nuller() };
				}
				if (pro["args"]["transport"].ToString() == "polling")
					return Parse_poll(pro);
				if (pro["args"]["transport"].ToString() == "websocket" && pro["args"]["sid"].IsNull())
				{
					return { EventType::SOCKETIOOPEN, buildHandshake("", "", pro["sec-websocket-key"], ProtocolType::SOCKETIO, false) ,Nuller() };
				}
			}
			if (pro["upgrade"] == "websocket" || pro["upgrade"] == "socket") {
				return { EventType::SOCKETHANDSHAKE, buildHandshake("", "", pro["sec-websocket-key"], pro["upgrade"] == "websocket" ? ProtocolType::WEBSOCKET : ProtocolType::SOCKET, false) ,Nuller() };
			}
			if (!pro["method"].IsNull())
			{
				return { EventType::HTTPREQUEST, httpstr , pro };
			}
			return { EventType::ERROREVENT, buildHttpErr("Invalid HTTP request") , Nuller() };
		}
		else
		{
			if (ptype == ProtocolType::SOCKETIO)
				return Parse_poll(pro, ptype);
			if (ptype == ProtocolType::WEBSOCKET || ptype == ProtocolType::SOCKET) {
				if (pro["status"] == 101) {
					byte sha1[20];
					SHA1(sha1, clientKey + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11");
					String Sec = Base64Encode((char*)sha1, 20);
					if (Sec != pro["sec-websocket-accept"])
						return { EventType::ERROREVENT, "", Nuller() };
					return { EventType::SOCKETHANDSHAKE, Nuller(), Nuller() };
				}
			}
			if (ptype == ProtocolType::HTTPSOCKET)
			{
				if (!pro["status"].IsNull())
					return { EventType::HTTPRESPONSE, httpstr , pro };
			}
		}
		return { EventType::ERROREVENT, buildHttpErr("Invalid HTTP request"), Nuller() };
	}
}
HttpProtocol Protocol::Parses(const String& httpstr, const Httplib::HttpRecvStatus& status, ProtocolType ptype)
{
	if(status.header_complete)
		return Parse_HttpHeadPro(httpstr, status, ptype);
	if(!status.header_complete && status.is_chunked)
		return { EventType::HTTPCHUNKED, httpstr, Nuller()};
	return { EventType::ERROREVENT, buildHttpErr("Incomplete HTTP message"), Nuller() };
}

void NetLibrary::Protocol::ParseHttpPro(One<SockPacket> pack, ProtocolType ptype)
{
	size_t extra_tail_len = 0;
	Httplib::HttpRecvStatus status = Httplib::CheckHttpReceiveCompleteness(*pack, pack->get_wirte_num(), &extra_tail_len);
	if (extra_tail_len > 0)
	{
		// 存在粘包数据，记录多余数据长度
		One<SockPacket> newpack = pick(pack->Clone());
		newpack->wirte(pack->offsetPtr(pack->get_wirte_num() - extra_tail_len), extra_tail_len);
		pack->ReduceToLeft(extra_tail_len);
		WhenEvent(EventType::DATAEVENT, newpack, false);
	}
	if (status.need_more > 0) {
		// 需要更多数据，等待下一次接收
		WhenEvent(EventType::DATAEVENT, pack, false);
		return;
	}
	else
	{
		if (status.message_complete) {
			// 已完整接收HTTP消息，进行处理
			One<SockPacket> newpack = pick(pack->Clone());
			newpack->SetPacketType(pack->GetPacketType() == PacketType::RECV ? PacketType::SEND : PacketType::SSL_SEND);
			HttpProtocol httpPro = Parses(pack->tostring(), status, ptype);
			newpack->wirte(httpPro.data);
			if(httpPro.etype != EventType::NONE)
				WhenEvent(httpPro.etype, newpack, httpPro.val);
		}
		else
		{
			if (ptype == ProtocolType::SOCKETIO)
			{
				WhenEvent(EventType::SIODATAEVENT, pack, true);
				return;
			}
			// HTTP消息不完整，但已接完当前数据，可能是协议错误，关闭连接或发送错误响应
			One<SockPacket> backpack = pick(pack->Clone());
			backpack->wirte(buildHttpErr("Incomplete HTTP message"));
			WhenEvent(EventType::CLOSEDEVENT, backpack, false);
		}
	}
	
}
