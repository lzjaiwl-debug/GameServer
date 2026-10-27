#ifndef _NetLibrary_databuf_h_
#define _NetLibrary_databuf_h_
namespace NetLibrary {
enum class frame_type
{
    frame_open = 0,
    frame_close = 1,
    frame_ping = 2,
    frame_pong = 3,
    frame_message = 4,
    frame_upgrade = 5,
    frame_noop = 6,
	frame_error = -1,
	frame_unknown = 255
}; 
enum class state_type
{
    type_connect = 0,
    type_disconnect = 1,
    type_event = 2,
    type_ack = 3,
    type_error = 4,
    type_binary_event = 5,
    type_binary_ack = 6,
	type_error_state = -1,
	type_unknown = 255
};
inline int SgnCompare(frame_type a, frame_type b) { return NumberCompare__((int)a, (int)b); }
inline int SgnCompare(state_type a, state_type b) { return NumberCompare__((int)a, (int)b); }
struct json_data : public Json
{
	Value GetJson()
	{
		return ParseJSON(~*this);
	}
};
struct sio_message
    : ValueType<sio_message, 514, Comparable<sio_message, Moveable<sio_message>>> // 514 需全局唯一且 < 0x8000000
{
    typedef sio_message CLASSNAME;
	mutable String cache;
    String _event;
    int _num = 1;
    frame_type _type;
    state_type _state;
	int ackid = -1;
    Value json;

    sio_message() {}
    sio_message(frame_type type, state_type state = state_type::type_connect) : _type(type), _state(state) {}
    sio_message(EMIEVENT ev, frame_type type, state_type state = state_type::type_connect, int ack = -1) : _event(ev), _type(type), _state(state), ackid(ack) {}
    sio_message(EMIEVENT ev, int num, frame_type type, state_type state = state_type::type_connect, int ack = -1) : _event(ev), _num(num), _type(type), _state(state), ackid(ack) {}

	sio_message& AddVal(const ValueMap& m)
	{
		if(json.IsNull() && !_event.IsEmpty())
			json.Add(_event);
		json.Add(m);
		return *this;
	}
	sio_message& AddVal(const Value& val)
	{
		if(json.IsNull() && !_event.IsEmpty())
			json.Add(_event);
		json.Add(val);
		return *this;
	}
	sio_message& SetVal(const Value& key, const Value& val)
	{
		if(json.IsNull() && !_event.IsEmpty())
			json.Add(_event);
		if(!_event.IsEmpty())
			json.At(json.GetCount()).GetAdd(key) = val;
		else
			json.GetAdd(key) = val;
        return *this;
	}
	sio_message& SetVal(const int idx, const Value& val)
    {
        if(json.IsNull() && !_event.IsEmpty())
			json.Add(_event);
		json.At(idx)=val;
        return *this;
    };
    sio_message& SetVal(const int idx, const Value& key, const Value& val)
    {
        if(json.IsNull() && !_event.IsEmpty())
			json.Add(_event);
		json.At(idx).GetAdd(key) = val;
        return *this;
    };
	void get_msgbuffer() const
    {
		if (!cache.IsEmpty())
			cache.Clear();
        if(_type != frame_type::frame_message)
        {
			cache << (int)_type + '0';
            if(!json.IsNull())
				cache << AsJSON(json);
            return;
        }
		Value payload = json;
		cache << (int)_type + '0' << (int)_state + '0';
		if(_state == state_type::type_binary_event || _state == state_type::type_binary_ack) {
			cache << _num + '0' << '-';
			if(payload.IsNull() && !_event.IsEmpty())
				payload.Add(_event);
			for(int i = 0;i<_num;i++) {
				ValueMap ph;
				ph.Add("_placeholder", true);
				ph.Add("num", i);
				payload.Add(ph);
			}
		}
		if(ackid >= 0 && (_state == state_type::type_event || _state == state_type::type_ack
			|| _state == state_type::type_binary_event || _state == state_type::type_binary_ack))
			cache << ackid;
		if(!payload.IsNull())
			cache << AsJSON(payload);
    };
	bool parse_msgbuffer(const String& data)
	{
		int index = 0;
		ackid = -1;
		if(data[index] - '0' > (int)frame_type::frame_noop)
			return false;
		_type = (frame_type)(data[index++] - '0');
		if(_type == frame_type::frame_message)
		{
			_state = (state_type)(data[index++] - '0');
			if(_state == state_type::type_binary_event || _state == state_type::type_binary_ack) {
				_num = 0;
				while(data[index] != '-') {
					_num = _num * 10 + (data[index++] - '0');
				}
				index++;
			}
			if(_state == state_type::type_event || _state == state_type::type_ack
				|| _state == state_type::type_binary_event || _state == state_type::type_binary_ack)
			{
				int tmp_ackid = 0;
				bool has_ackid = false;
				while(index < data.GetLength() && data[index] >= '0' && data[index] <= '9') {
					tmp_ackid = tmp_ackid * 10 + (data[index++] - '0');
					has_ackid = true;
				}
				ackid = has_ackid ? tmp_ackid : -1;
			}
		}
		if(index < data.GetLength())
		{
			Value v = ParseJSON(data.Mid(index));
			json = v;
		}
		return true;
	}
	int Compare(const sio_message& o) const 
	{ 
		return CombineCompare(_event, o._event)(_num, o._num)(_type,o._type)(_state,o._state)(ackid,o.ackid)(json,o.json);
	}
	int PolyCompare(const Value& v) const {
		// Compare against Value containing sio_message
		const sio_message& o = v.To<sio_message>();
		return Compare(o);
	}
	
    void Jsonize(JsonIO& io)
	{
		// store/load fields
		io("event", _event)("num", _num)("ackid", ackid);
		if(io.IsLoading()) {
			int ti = 0, si = 0;
			io("type", ti)("state", si)("json", json);
			_type = (frame_type)ti;
			_state = (state_type)si;
		}
		else {
            // Use temporary variables to satisfy JsonIO::operator()(key, T&)
            int ti = (int)_type;
            int si = (int)_state;
            io("type", ti)("state", si)("json", json);
		}
	}
	void Xmlize(XmlIO& xio)
	{
		xio("event", _event)("num", _num)("ackid", ackid);
		if(xio.IsLoading()) {
			int ti = 0, si = 0;
			xio("type", ti)("state", si)("json", json);
			_type = (frame_type)ti;
			_state = (state_type)si;
		}
		else {
			int ti = (int)_type;
			int si = (int)_state;
			xio("type", ti)("state", si)("json", json);
		}
	}
	void Serialize(Stream& s) 
	{
		int _type_int = -1;
		int _state_int = -1;
		s% _event;
		if(s.IsStoring()) {
			_type_int = (int)_type;
			_state_int = (int)_state;
		}
		else
		{
			s % _type_int % _state_int;
			_type = (frame_type)_type_int;
			_state = (state_type)_state_int;
		}
		s% _num % ackid % json;
	}
	unsigned GetHashValue() const { return CombineHash(this); }
	String ToString() const
    {
		get_msgbuffer();
		return cache;
    }
    sio_message& operator=(const Value& v) {
		json  = v;  // 调用基类赋值
        return *this;
    };
	operator String () const
    {
        return ToString();
    };

	const Value& operator[](const char* str) const
	{
		return json[str];
	};
	const char* operator ~() const
	{
		get_msgbuffer();
		return ~cache;
	}
	operator const char * () const
    {
		get_msgbuffer();
		return ~cache;
    };
    // 添加 Data 转换
	operator AssetLib::BinaryData&& () const {
        String str = ToString();
        return pick(AssetLib::BinaryData(str));
    }
	bool IsNullInstance() const { return json.IsNull(); }
    // U++ Value integration: allow implicit store into Value and retrieval via Get<sio_message>()
    operator Value() const {
        return RichValue<sio_message>(*this);
    }
    static sio_message FromValue(const Value& v) {
        return v.Get<sio_message>();
    }
    void free()
    {
		json = Value();
    };

    // Ensure U++ Value type registration for sio_message
    static void RegisterValueType()
    {
        static bool done = false;
        if (!done) {
            Value::Register<sio_message>(); // 不传 name，类型号固定为 514，满足 w < 0x8000000
            done = true;
        }
    }

private:
    // Static registrar to auto-register at startup
    struct _SioMessageRegistrar {
        _SioMessageRegistrar() { sio_message::RegisterValueType(); }
    };
    static inline _SioMessageRegistrar _registrar{};
};
class SockPacket {
private:
	NetSSL* ussl = NULL;
	SOCKET ufd = INVALID_SOCKET;//代表客户端的socket对象
    PacketType pa_type = PacketType::RECV;//标识包类型是发送还是接收
    bool isHandshake = false;//标识包类型是已握手类型还是未握手类型
    bool isBinary = false;//为true 则是二进制数据，否则为文本数据
    size_t wirte_num = 0;//表示缓冲数据已写入多少
	size_t read_num = 0;//表示缓冲数据已读取多少
#ifdef PLATFORM_WIN32
	WSABUF _wsaBuf;
#endif // PLATFORM_WIN32
protected:
	AssetLib::MemoryBlock* buf = NULL;//代表申请的内存块
	AssetLib::MemoryPool* pool = NULL;//代表统一的内存管理池，用于申请和释放内存块
public:
	~SockPacket(){
		freepack();
	}

	SockPacket()
	{
	}
	/// <summary>
	/// 构造方法
	/// </summary>
	/// <param name="p">内存池对象</param>
	SockPacket(AssetLib::MemoryPool* p):SockPacket() {
		pool = p;
	}
	/// <summary>
	/// 构造方法
	/// </summary>
	/// <param name="fd">用户socket对象</param>
	/// <param name="type">包的类型 是发送还是接收</param>
	/// <param name="p">内存池对象</param>
	SockPacket(SOCKET fd, PacketType type, AssetLib::MemoryPool* p) : SockPacket(p){
		ufd = fd;
		pa_type = type;
	}
	/// <summary>
	/// 构造方法
	/// </summary>
	/// <param name="s">用户的SSL对象</param>
	/// <param name="fd">用户socket对象</param>
	/// <param name="type">包的类型 是发送还是接收</param>
	/// <param name="p">内存池对象</param>
	SockPacket(NetSSL* s, SOCKET fd, PacketType type, AssetLib::MemoryPool* p) : SockPacket(fd, type, p){
		ussl = s;
	}
	/// <summary>
/// 释放缓冲区（如果已分配），将 buf 设为 NULL，并将写/读计数器重置为 0。
/// </summary>
	void freespace()
	{
		wirte_num = 0;
		read_num = 0;
		if (buf != NULL && *buf != NULL)
			pool->deallocate(buf);
		buf = NULL;
	}
	/// <summary>
	/// 释放与数据包相关的资源并重置全局状态：将 ufd 置为 -1；如果 buf 和 *buf 非空，则通过 pool->deallocate 释放缓冲区；随后将 buf、pool 和 ussl 置为 NULL，并将 wirte_num 与 read_num 置为 0。
	/// </summary>
	void freepack()
	{
		ufd = -1;
		if (buf != NULL && *buf != NULL)
			pool->deallocate(buf);
		buf = NULL; pool = NULL; ussl = NULL;
		wirte_num = 0;
		read_num = 0;
	}
	One<SockPacket> Clone()
	{
		One<SockPacket> newpack;
		newpack.Create<SockPacket>();
		newpack->set_fd(ufd);
		newpack->set_ssl(ussl);
		newpack->SetBinary(isBinary);
		newpack->SetHandshake(isHandshake);
		newpack->pool = pool;
		return pick(newpack);
	}
	/// <summary>
	/// 检查对象是否为空（当 ufd 为 -1 且 buf 和 pool 均为 NULL 时视为为空）。
	/// </summary>
	/// <returns>如果 ufd == -1 且 buf 和 pool 均为 NULL，则返回 true；否则返回 false。</returns>
	bool isempty()
	{
		return ufd == -1 && buf == NULL && pool == NULL;
	}
	/// <summary>
	/// 获取已写入数据的大小指针
	/// </summary>
	/// <returns>指针</returns>
	size_t* get_wirte_num_ptr()
	{
		return &wirte_num;
	}
	/// <summary>
	/// 获取已写入数据的大小
	/// </summary>
	/// <returns></returns>
	size_t get_wirte_num()
	{
		return wirte_num;
	}
	/// <summary>
	/// 是否二进制数据
	/// </summary>
	/// <returns>返回真假</returns>
	bool IsBinary()
	{
		return isBinary;
	}
	/// <summary>
	/// 设置为二进制数据
	/// </summary>
	/// <param name="is">真假</param>
	void SetBinary(bool is)
	{
		isBinary = is;
	}
	/// <summary>
	/// 组建数据包的用户是否已完成握手
	/// </summary>
	/// <returns>返回真假</returns>
	bool IsHandshake()
	{
		return isHandshake;
	}
	/// <summary>
	/// 设置组建数据包为已完成握手
	/// </summary>
	/// <param name="is">返回真假</param>
	void SetHandshake(bool is)
	{
		isHandshake = is;
	}
	/// <summary>
	/// 设置组建该数据包的用户SOCKET对象
	/// </summary>
	/// <param name="fd">SOCKET对象</param>
	void set_fd(SOCKET fd)
	{
		ufd = fd;
	}
	/// <summary>
	/// 返回组建该数据包的用户SOCKET对象
	/// </summary>
	/// <returns>SOCKET对象</returns>
	SOCKET get_fd()
	{
		return ufd;
	}
	/// <summary>
	/// 返回组建该数据包的用户SSL对象
	/// </summary>
	/// <returns>SSL对象</returns>
	NetSSL* get_ssl()
	{
		return ussl;
	}
	/// <summary>
	/// 设置组建该数据包的用户SSL对象
	/// </summary>
	/// <param name="ssl">SSL对象</param>
	void set_ssl(NetSSL* ssl)
	{
		ussl = ssl;
	}
	/// <summary>
	/// 获取数据包剩余可写入的位置的指针
	/// </summary>
	/// <returns>指针</returns>
	char* get_wirte_ptr()
	{
		return buf ? *buf + wirte_num : nullptr;
	}
	/// <summary>
	/// 获取剩余可写入空间
	/// </summary>
	/// <returns>返回空间大小</returns>
	int get_free_space()//获取剩余可写空间
	{
		if (!buf)
			return 0;
		return buf->size - wirte_num < 0 ? -1 : buf->size - wirte_num;
	}
	/// <summary>
	/// 将数据包空间追加指定大小，不影响已写入数据
	/// </summary>
	/// <param name="size">指定追加的大小</param>
	void addset_free_space(int size)
	{
		if(get_free_space() >= size)
			return;
		AssetLib::MemoryBlock* tmp = pool->allocate(buf->size + (size - (buf->size - wirte_num)));
		memcpy(*tmp, *buf, wirte_num);
		pool->deallocate(buf);
		buf = tmp;
	}
	/// <summary>
	/// 向右缩减大小并删除数据
	/// </summary>
	/// <param name="size">缩减的大小</param>
	void ReduceToRight(int size)//向右缩减大小并删除数据
	{
		if(size>= buf->size)
			return;
		AssetLib::MemoryBlock* tmp = pool->allocate(buf->size - size);
		memcpy(*tmp, *buf + size, buf->size - size);
		pool->deallocate(buf);
		buf = tmp;
		read_num -= read_num >= size ? size : read_num;
		wirte_num -= wirte_num >= size ? size : wirte_num;
	}
	/// <summary>
	/// 向左缩减大小并删除数据
	/// </summary>
	/// <param name="size">缩减的大小</param>
	void ReduceToLeft(int size)//向左缩减大小并删除数据
	{
		if(size>= buf->size)
			return;
		AssetLib::MemoryBlock* tmp = pool->allocate(buf->size - size);
		memcpy(*tmp, *buf, buf->size - size);
		pool->deallocate(buf);
		buf = tmp;
		read_num -= read_num - (buf->size - size) > 0 ? read_num - (buf->size - size) : 0;
		wirte_num -= wirte_num - (buf->size - size) > 0 ? wirte_num - (buf->size - size) : 0;
	}
	/// <summary>
	/// 通过调用 ReduceToLeft(buf->size - wirte_num) 将内部缓冲区向左收缩以移除多余的空闲部分。
	/// </summary>
	void RemoveEmpty()
	{
		ReduceToLeft(buf->size - wirte_num);
	}
	/// <summary>
	/// 移除已读取的数据空间，通过调用 ReduceToRight(read_num) 实现。
	/// </summary>
	void RemoveReadSpaces()
	{
		ReduceToRight(read_num);
	}
	/// <summary>
	/// 设置数据包的类型，写入还是接收
	/// </summary>
	/// <param name="type"></param>
	void SetPacketType(PacketType type)
	{
		pa_type = type;
	}
	/// <summary>
	/// 获取数据包的模式
	/// </summary>
	/// <returns>返回写入还是接收枚举</returns>
	PacketType GetPacketType()
	{
		return pa_type;
	}
	/// <summary>
	/// 向记录已写入大小的部分追加大小值
	/// </summary>
	/// <param name="size">追加大小值</param>
	void add_wirte_num(size_t size)
	{
		if(wirte_num + size <= buf->size)
			wirte_num += size;
	}
	/// <summary>
	/// 已读取数据大小设置
	/// </summary>
	/// <param name="size"></param>
	void add_read_num(size_t size)
	{
		if(read_num + size <= buf->size)
			read_num += size;
	}
	/// <summary>
	/// 获取全局变量 read_num 的当前值。
	/// </summary>
	/// <returns>返回 read_num 的值，类型为 size_t。</returns>
	size_t get_read_num()
	{
		return read_num;
	}
	/// <summary>
	/// 返回未读取的数据数量（wirte_num 与 read_num 之差）。
	/// </summary>
	/// <returns>表示未读取项数量的 size_t 值，等于 wirte_num - read_num。注意：由于返回类型为无符号，若 wirte_num 小于 read_num，结果将发生下溢并成为一个很大的值。</returns>
	size_t get_not_read_num()
	{
		return wirte_num - read_num;
	}
	/// <summary>
	/// 返回指向缓冲区当前读取位置的只读字符指针（基于外部或全局变量 buf 和 read_num）。
	/// </summary>
	/// <returns>指向 buf 中从偏移 read_num 开始的位置的 const char*，表示只读字符序列的当前位置。</returns>
	const char* get_read_ptr()
	{
		return *buf + read_num;
	}
	/// <summary>
	/// 返回指向缓冲区当前读取游标的指针。
	/// </summary>
	/// <returns>指向缓冲区当前位置的 char* 指针（等同于 *buf + read_num）。</returns>
	char* get_read_ptr_cursor()
	{
		return *buf + read_num;
	}
	/// <summary>
	/// 申请固定大小的空间（仅有空间，没有写入）
	/// </summary>
	/// <param name="size">空间大小</param>
	/// <returns>返回是否成功</returns>
	bool ApplyBuf(size_t size, bool isreset = false)//申请固定大小的空间
	{
		if (buf && !isreset)
			return false;
		if(!buf)
			return (buf = pool->allocate(size));
		else
		{
			if (get_free_space() == size)
			{
				memset(*buf, 0, size);
			}
			else
			{
				AssetLib::MemoryBlock* tmp = pool->allocate(size);
				pool->deallocate(buf);
				buf = tmp;
			}
			read_num = wirte_num = 0;
			return buf != NULL;
		}
	}
	/// <summary>
	/// 写入固定大小的数据
	/// </summary>
	/// <param name="data">数据指针</param>
	/// <param name="size">数据大小</param>
	/// <returns>是否成功</returns>
	// 在 SockPacket 类中添加支持 void* 数据指针的 wirte 重载
	bool wirte(const void* data, int size)
	{
		if (!buf && !ApplyBuf(size))
			return false;
		if (get_free_space() < size)
			addset_free_space(size);
		memcpy(*buf + wirte_num, data, size);
		wirte_num += size;
		return true;
	}
	/// <summary>
	/// 写入String数据
	/// </summary>
	/// <param name="data">String数据</param>
	/// <returns>是否成功</returns>
	bool wirte(const String& data)//写入固定大小的数据
	{
		return wirte(~data,data.GetLength());
	}
	/// <summary>
	/// 写入int数据
	/// </summary>
	/// <param name="data"></param>
	/// <returns></returns>
	bool wirte(int16 data)//写入固定大小的数据
	{
		return wirte((char*)&data,sizeof(int64));
	}
	/// <summary>
	/// 写入int64数据
	/// </summary>
	/// <param name="data"></param>
	/// <returns></returns>
	bool wirte(int64 data)//写入固定大小的数据
	{
		return wirte((char*)&data,sizeof(int64));
	}
	/// <summary>
	/// 写入Data结构体数据
	/// </summary>
	/// <param name="data">结构体数据</param>
	/// <returns></returns>
	bool wirte(const AssetLib::BinaryData& data)
	{
		return wirte((char*)data.data, data.size);
	}
	/// <summary>
	/// 从data偏移位置写入固定大小数据到pack
	/// </summary>
	/// <param name="data">数据</param>
	/// <param name="offset">数据指针偏移量</param>
	/// <param name="size">大小</param>
	/// <returns></returns>
	bool wirte(const AssetLib::BinaryData& data, int offset, int size)//从data偏移位置写入固定大小数据到pack
	{
		return wirte((char*)data.data + offset, size);
	}
	/// <summary>
	/// 从包中拷贝偏移量位置的size大小的数据
	/// </summary>
	/// <param name="dest">要拷贝的堆指针</param>
	/// <param name="offset">pack数据指针偏移</param>
	/// <param name="size">数据大小</param>
	void copy(char* dest, int offset, int size)//从包中拷贝偏移量位置的size大小的数据
	{
		memcpy(dest, *buf + offset, size);
	}
	/// <summary>
	/// 将内部缓冲区从当前读取位置开始的 size 字节复制到目标缓冲区，并将读取位置向前移动 size 字节。
	/// </summary>
	/// <param name="dest">目标缓冲区指针，接收复制的数据。该缓冲区必须至少可容纳 size 字节，未满足时行为未定义。</param>
	/// <param name="size">要复制的字节数（以字节为单位）。</param>
	void OrderedCopy(char* dest, int size)
	{
		memcpy(dest, *buf + read_num, size);
		add_read_num(size);
	}
	/// <summary>
	/// 将二进制数据转换为string
	/// </summary>
	/// <returns></returns>
	String tostring()
	{
		return buf->tostring();
	}
	/// <summary>
	/// 获取固定偏移处的string转换数据
	/// </summary>
	/// <param name="offset">偏移量</param>
	/// <returns>string数据</returns>
	String tostring(int offset)
	{
		return buf->tostring(offset);
	}
	/// <summary>
	/// 将数据复制到Data结构体
	/// </summary>
	/// <param name="d"></param>
	void refData(AssetLib::BinaryData& d)//直接复制
	{
		d = *buf;
	}
	/// <summary>
	/// 获取偏移固定位置的pack数据
	/// </summary>
	/// <param name="num"></param>
	/// <returns></returns>
	char* offsetPtr(int num)
	{
		return *buf + num;
	}
	operator char* ()
	{
		return *buf;
	}
	operator int ()
	{
		return buf ? (int)buf->size : 0;;//总是返回剩余数据量
	}

	operator String ()
	{
		return buf ? buf->tostring() : String();
	}
#ifdef PLATFORM_WIN32
	operator WSABUF* ()
	{
		_wsaBuf.len = (ULONG)get_free_space();
		_wsaBuf.buf = get_wirte_ptr();
		return &_wsaBuf;
	}
	WSABUF* operator ~ ()
	{
		_wsaBuf.len = (ULONG)this->get_not_read_num();
		_wsaBuf.buf = buf ? (*buf + read_num) : nullptr;
		return &_wsaBuf;
	}
	operator PVOID ()
	{
		return buf ? (PVOID)(*buf + wirte_num) : nullptr;
	}
#endif // PLATFORM_WIN32
};
class sio_packet{
private:
	One<SockPacket> pack;
public:
	sio_packet(){}
	sio_packet(One<SockPacket>&& p){pack = pick(p);}
	sio_packet(const char* str, One<SockPacket>&& p):sio_packet(pick(p))
	{
		String ss(str);
		pack->wirte(ss);
	}
	frame_type get_frame()
	{
		if((char*)*pack == NULL)
			return frame_type::frame_error;
		int flag = ((char*)*pack)[0] - '0';
		if(flag > (int)frame_type::frame_noop)
			return frame_type::frame_unknown;
		return (frame_type)flag;
	}
	state_type get_state()
	{
		if((char*)*pack == NULL || get_frame() != frame_type::frame_message)
			return state_type::type_error_state;
		int flag = ((char*)*pack)[1] - '0';
		if (flag > (int)state_type::type_binary_ack)
			return state_type::type_unknown;
		return (state_type)flag;
	}
	int get_num()///获取包数量，仅对二进至数据有效
	{
		if (get_frame() == frame_type::frame_message && (get_state() == state_type::type_binary_event || get_state() == state_type::type_binary_ack))
		{
			int index = 2;
			int num = 0;
			while (((char*)*pack)[index] >= '0' && ((char*)*pack)[index] <= '9')
				num = num * 10 + (((char*)*pack)[index++] - '0');
			return num;
		}
		return -1;
	}
	int get_ackid()
	{
		if((char*)*pack == NULL || get_frame() != frame_type::frame_message)
			return -1;
		state_type st = get_state();
		if(st != state_type::type_event && st != state_type::type_ack
			&& st != state_type::type_binary_event && st != state_type::type_binary_ack)
			return -1;

		int index = 2;
		if(st == state_type::type_binary_event || st == state_type::type_binary_ack) {
			while (((char*)*pack)[index] >= '0' && ((char*)*pack)[index] <= '9')
				index++;
			if(((char*)*pack)[index] == '-')
				index++;
		}

		int ackid = 0;
		bool has_ackid = false;
		while (((char*)*pack)[index] >= '0' && ((char*)*pack)[index] <= '9') {
			ackid = ackid * 10 + (((char*)*pack)[index++] - '0');
			has_ackid = true;
		}
		return has_ackid ? ackid : -1;
	}
	int get_data_offset()
	{
		if((char*)*pack == NULL)
			return 0;
		if(get_frame() != frame_type::frame_message)
			return 1;

		int index = 2;
		state_type st = get_state();
		if(st == state_type::type_binary_event || st == state_type::type_binary_ack) {
			while (((char*)*pack)[index] >= '0' && ((char*)*pack)[index] <= '9')
				index++;
			if(((char*)*pack)[index] == '-')
				index++;
		}
		if(st == state_type::type_event || st == state_type::type_ack
			|| st == state_type::type_binary_event || st == state_type::type_binary_ack) {
			while (((char*)*pack)[index] >= '0' && ((char*)*pack)[index] <= '9')
				index++;
		}
		return index;
	}
	Value get_data()
	{
		int offset = get_data_offset();
		if(get_num() != -1)
			return ParseJSON(pack->tostring(offset));
		if(get_frame() == frame_type::frame_message)
		{
			if(*pack > offset)
				return ParseJSON(pack->tostring(offset));
			return Nuller();
		}
		if(get_frame() == frame_type::frame_ping || get_frame() == frame_type::frame_pong)
		{
			if(*pack > 1)
				return pack->tostring(1);
		}
		if (*pack > 0)
			return pack->tostring();
		return Nuller();
	}
};
};
#endif
