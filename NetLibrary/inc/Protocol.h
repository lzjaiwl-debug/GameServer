#ifndef _NetLibrary_Socketio_h_
#define _NetLibrary_Socketio_h_
namespace NetLibrary {
struct HttpProtocol {
	EventType etype = EventType::NONE;
	String data;
	Value val;
	HttpProtocol() {}
	HttpProtocol(HttpProtocol& hp)
	{
		etype = hp.etype;
		data = hp.data;
		val = hp.val;
	}
	HttpProtocol(EventType et, const String& d, Value v)
		: etype(et),
		data(d),
		val(v)
	{
	}
};
class Protocol{
private:
	SockType type = SockType::NONE;
	Vector<String> sio_sessions;
	String clientKey;
	String GetSocketKey();
	Value Parse_Handshake(const String& httpstr, const Httplib::HttpRecvStatus& status);
	HttpProtocol Parse_poll(const Value& pro, ProtocolType ptype = ProtocolType::CONNECT);
	HttpProtocol Parse_HttpHeadPro(const String& httpstr, const Httplib::HttpRecvStatus& status, ProtocolType ptype = ProtocolType::CONNECT);
	HttpProtocol Parses(const String& httpstr, const Httplib::HttpRecvStatus& status, ProtocolType ptype = ProtocolType::CONNECT);
protected:
	String buildHandshake(String pyload, String sid, String key, ProtocolType ptype, bool ispoll = true, const char* method = "GET");
	String buildHttpErr(const String& err);
	String buildSioPollErr(const String& err);
	Event<EventType, One<SockPacket>&, Value> WhenEvent;
	void ParseHttpPro(One<SockPacket> pack, ProtocolType ptype = ProtocolType::CONNECT);
public:
	Protocol(){}
	Protocol(SockType st) : type(st){}
	
};
};
#endif
