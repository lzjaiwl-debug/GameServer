#ifndef _NetLibrary_httpdef_h_
#define _NetLibrary_httpdef_h_
namespace NetLibrary {
 
struct keyvaluepair : Moveable<keyvaluepair>
{
	std::string key;
	std::string val;
	keyvaluepair() = default;
	keyvaluepair(const char* k,const char* v):key(k),val(v){}
	void Serialize(AssetLib::SerializerFactory& s)
	{
		s % key % val;
	}
};
struct Cookie : Moveable<Cookie>
{
	Vector<keyvaluepair> list;
	Cookie() {}
	Cookie(Cookie&& c) {
		list = pick(c.list);
	}
	void Serialize(AssetLib::SerializerFactory& s)
	{
		int num = list.GetCount();
		s % num;
		if(list.GetCount() == 0 && num != 0)
			list.SetCount(num);
		for(auto& a: list)
			s % a;
	}
	String operator [] (const char * key)
	{
		for(auto& a:list)
			if(a.key == key)
				return a.val;
		return Nuller();
	}
	void Set(const char* key, const char* val)
	{
		for(auto& a:list)
			if(a.key == key) { a.val = val; return; }
		list.Add({key, val});
	}
	void Remove(const char* key)
	{
		for(int i = list.GetCount() - 1; i >= 0; --i)
			if(list[i].key == key) { list.Remove(i); return; }
	}
};
struct CookieBox : Moveable<CookieBox>//保存cookie
{
	std::string dom;//域名
	VectorMap<String,Cookie> list;
	
	CookieBox() = default;
	CookieBox(const char* str):dom(str){}
	
	void Serialize(AssetLib::SerializerFactory& s)//序列化方法
	{
		s % dom;
		int num = list.GetCount();
		s % num;
		if(list.GetCount() == 0 && num != 0)
			list.Reserve(num);
		for(auto& a: list)
			s % a;
	}
	Cookie* operator [] (const char * sid)
	{
		if(list.Find(sid) >= 0)
			return &list.Get(sid);
		return nullptr;
	}
	Cookie* Add(const String& sid)
	{
		// Ensure key is created/copied safely and return pointer to stored Cookie
		return &list.GetPut(sid);
	}

	void Add(const String& sid, Cookie&& c)
	{
		list.AddPick(sid, pick(c));
	}

	void Del(const char* sid)
	{
		list.RemoveKey(sid);
	}
	/// <summary>
	/// 查找同一客户端IP最近创建的cookie，用于并发请求复用
	/// </summary>
	/// <param name="client_ip">客户端IP地址</param>
	Cookie* FindByClientIp(const char* client_ip)
	{
		if(!client_ip || !*client_ip || list.GetCount() == 0)
			return nullptr;
		for(int i = list.GetCount() - 1; i >= 0; --i)
		{
			if(list[i]["_client_ip"] == client_ip)
				return &list[i];
		}
		return nullptr;
	}
};
struct CookieList{
	Vector<CookieBox> list;
	void Serialize(AssetLib::SerializerFactory& s)//序列化方法
	{
		int num = list.GetCount();
		s % num;
		if(list.GetCount() == 0 && num != 0)
			list.SetCount(num);
		for(auto& a: list)
			s % a;
	}
	CookieBox* Add(const char* str)
	{
		return &list.Add(str);
	}
	bool IsDomain(const char * str)
	{
		for(auto& a: list)
			if(a.dom == str)
				return true;
		return false;
	}
	/// <summary>
	/// 获取域名对应的cookie 集合
	/// </summary>
	/// <param name="str">域名</param>
	/// <returns>集合指针</returns>
	CookieBox* operator [] (const char * str)
	{
		for(auto& a: list)
			if(a.dom == str)
				return &a;
		return nullptr;
	}
	/// <summary>
	/// 搜索指定域名下 指定id的cookie集合
	/// </summary>
	/// <param name="dom">域名名称</param>
	/// <param name="sid">id</param>
	/// <returns>cookie集合</returns>
	Cookie* FindSeeion(const char* dom, const char* sid)
	{
		if (!dom || !sid) return nullptr;
		CookieBox * box = (*this)[dom];
		if (!box)
			return nullptr;
		Cookie* co = (*box)[sid];
		if (!co)
			return nullptr;
		return co;
	}
	/// <summary>
	/// 查找域名相等的用户cookie
	/// </summary>
	/// <param name="sid"></param>
	/// <returns></returns>
	Cookie* FindSeeion(const char* sid)
	{
		for (auto& b : list)
		{
			for (auto& c : b.list)
			{
				if (String(b.dom.c_str()) == c["seesion_id"])
					return &c;
			}
		}
		return nullptr;
	}
	void DelSeesion(const char* dom, const char* sid)
	{
		if (!dom || !sid) return;
		CookieBox* box = (*this)[dom];
		if (box)
			box->list.RemoveIf([&](int i) { return box->list[i]["seesion_id"] == sid; });
	}
	void SerializeToDisk(const char* dom, const char* sid, const char* file)
	{
		
		Cookie* co = FindSeeion(dom, sid);
		if (co)
		{
			AssetLib::BinaryData bin;
			AssetLib::SerializerFactory::Serialize(*co, bin);
			String file_path = Httplib::MakeCookiePathStd(file, dom, sid);
			FileOut out(file_path);
			out.Put((const void*)bin.data, bin.size);
			out.Close();
			DelSeesion(dom, sid);
		}
	}
	void DeserializeFromDisk(const char* dom, const char* sid, const char* file)
	{
		if (!dom || !sid || !file) return;
		String path = Httplib::MakeCookiePathStd(file, dom, sid);
		if (!FileExists(path)) return;
		AssetLib::BinaryData d(0, GetFileLength(path));
		Cookie co;
		FileIn in(path);
		in.Get(d.data, d.size);
		AssetLib::SerializerFactory::Deserialize(co, d);
		this->Add(dom)->Add(sid, pick(co));
		in.Close();
		FileDelete(path);
	}
	void Clear()
	{
		list.Clear();
	}
};
// 磁盘缓存
struct DiskTask : Moveable<DiskTask> {
	SOCKET fd;                    // 连接描述符
	Httplib::StatusCode code;            // 响应类型
	String temp_file_path;      // 临时文件路径, istemp为真则表示该文件为缓存文件，使用后可删除；为假则表示用户指定的文件路径，不可删除
	size_t data_size;           // 数据大小
	bool istemp;                // 是否缓存文件
	njson body;                 // 记录扩展字段
	DiskTask(SOCKET s, Httplib::StatusCode c, const char* path, size_t size, bool is, njson json) : fd(s), code(c), temp_file_path(path), data_size(size), istemp(is), body(json){}
} ;
};
#endif
