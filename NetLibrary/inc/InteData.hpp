#ifndef _Comlibrary_InteData_h_
#define _Comlibrary_InteData_h_
namespace NetLibrary {
/*struct CacheData : Data
{
	bool isOwnership = true;
	CacheData(){}
	CacheData(const char* str) : Data(str){
	};
	CacheData(const String& s) : Data(s){}
	CacheData(PushData& d){
		len = d.len;
		data = d.data;
		isOwnership = false;
	}
	CacheData(CacheData&& d){
		Pick(pick(d));
	}
	CacheData(CacheData& d){
		NoPick(d);
	}
	void Pick(CacheData&& d)//移交所有权
	{
		len = d.len;
		data = d.data;
		isOwnership = true;
		d.isOwnership = false;
	}
	void NoPick(CacheData& d)//不移交所有权
	{
		len = d.len;
		data = d.data;
		isOwnership = false;
	}
	CacheData& operator = (Data& d)
	{
		len = d.len;
		data = d.data;
		isOwnership = false;
		return *this;
	}
	CacheData& operator = (PushData& d)
	{
		CacheData::Free();
		len = d.len;
		data = d.data;
		isOwnership = false;
		return *this;
	}
	CacheData& operator = (const String s)
	{
		AllocateMemory(s.GetLength(),s);
		return *this;
	}
	virtual void Free()
	{
		if(data!=NULL && isOwnership)
			::MemoryFree(data);
		data = NULL;
		len = 0;
	}
	virtual ~CacheData()
	{
		CacheData::Free();
	}
};*/

//template <class T>
//struct MoveData
//{
//	Data d;
//	void Serialize(InteData& s)
//	{
//		s % d;
//	}
//	void Set(const T& t)
//	{
//		T tmp(t);
//		d.Allocate(tmp.GetLength());
//		d.CopyData(0,tmp,tmp.GetLength());
//		//d.len = tmp.GetLength();
//		//d.data = (char*)::malloc(d.len);
//		//memcpy(d.data,tmp,d.len);
//	}
//	bool operator == (const T& x)
//	{
//		return T(d.data,d.len).IsEqual(x);
//	}
//	bool operator == (const T& x) const
//	{
//		return T(d.data,d.len).IsEqual(x);
//	}
//	MoveData(){};
//	MoveData(const T& t){Set(t);};
//	MoveData<T>& operator=(const T&& t)
//	{
//		Set(t);
//		return *this;
//	}
//	MoveData<T>& operator=(const char* c)
//	{
//		Set(T(c));
//		return *this;
//	}
//	String ToString()
//	{
//		return String(d.data,d.len);
//	}
//	operator T () const
//	{
//		return T(d.data,d.len);
//	}
//	operator T ()
//	{
//		return T(d.data,d.len);
//	}
//	T operator()() const
//	{
//		return T(d.data,d.len);
//	}
//	T operator()()
//	{
//		return T(d.data,d.len);
//	}
//	operator const char* () const
//	{
//		return ~T(d.data,d.len);
//	}
//	operator const char* ()
//	{
//		return ~T(d.data,d.len);
//	}
//};
//template <>
//struct MoveData<Value> : Moveable<MoveData<Value>>
//{
//private:
//	void Set(const Value& t)
//	{
//		if(d.data != NULL)
//			d.Free();
//		type = t.GetType();
//		switch(type)
//	    {
//		    case INT_V:
//		        d.Allocate(sizeof(int));
//		        d.CopyData(0,&t.Get<int>(),sizeof(int));
//		        break;
//		    case INT64_V:
//		        d.Allocate(sizeof(int64));
//		        d.CopyData(0,&t.Get<int64>(),sizeof(int64));
//		        break;
//		    case DOUBLE_V:
//		        d.Allocate(sizeof(double));
//		        d.CopyData(0,&t.Get<double>(),sizeof(double));
//		        break;
//		    case BOOL_V:
//		        d.Allocate(sizeof(bool));
//		        d.CopyData(0,&t.Get<bool>(),sizeof(bool));
//		        break;
//		    case STRING_V:
//		    case WSTRING_V:
//		        d = ~t;
//		        break; 
//		    case DATE_V:
//		        d.Allocate(sizeof(Date));
//		        d.CopyData(0,&t.Get<Date>(),sizeof(Date));
//		        break;
//		    case TIME_V:
//		        d.Allocate(sizeof(Time));
//		        d.CopyData(0,&t.Get<Time>(),sizeof(Time));
//		        break;
//		    default:
//			    break;
//	    }
//	}
//public:
//	Data d;
//	dword type;
//	void Serialize(InteData& s)
//	{
//		s % d % type;
//	}
//	MoveData(){};
//	MoveData(const Value& t){Set(t);}
//	MoveData(const char* str){Set(Value(str));}
//	template <class T>
//	void SetRefValue(const Value& t)
//	{
//		if(!t.Is<T>())
//			return;
//		T& tt = t.Get<T>();
//		d.Allocate(sizeof(tt));
//		d.CopyData(0,&tt,sizeof(tt));
//	}
//	//MoveData(String str){Set(Value(str));}
//	//MoveData(Uuid id){Set(Value(id));}
//	Value GetValue()
//	{
//		Value a;
//		switch(type)
//	    {
//		    case INT_V:
//		        a = *(int*)d.data;
//		        break;
//		    case INT64_V:
//		        a = *(int64*)d.data;
//		        break;
//		    case DOUBLE_V:
//		        a = *(double*)d.data;
//		        break;
//		    case BOOL_V:
//		        a = *(bool*)d.data;
//		        break;
//		    case STRING_V:
//		        a = String(d.data,d.len);
//		        break;
//		    case WSTRING_V:
//		        a = String(d.data,d.len).ToWString();
//		        break; 
//		    case DATE_V:
//		        a = *(Date*)d.data;
//		        break;
//		    case TIME_V:
//		        a = *(Time*)d.data;
//		        break;
//		    default:
//		        a = Nuller();
//			    break;
//	    }
//	    return a;
//	}
//	MoveData& operator=(const Value& t)
//	{
//		Set(t);
//		return *this;
//	}
//	MoveData& operator=(Value t)
//	{
//		Set(t);
//		return *this;
//	}
//	MoveData& operator=(const char* str)
//	{
//		Set(Value(str));
//		return *this;
//	}
//	String ToString() const
//	{
//		return String(d.data,d.len);
//	}
//	operator Value ()
//	{
//		return GetValue();
//	}
//	operator String ()
//	{
//		return GetValue();
//	}
//	operator const char * ()
//	{
//		return ~~GetValue();
//	}
//	static Data&& GetSerializeData(const char* str)
//	{
//		MoveData<Value> val = str;
//		Data data;InteData::Serialize(val,data);
//		return pick(data);
//	}
//};
//template <class T>
//struct RefData : MoveData<Value>
//{
//	RefData(const Value& t) : MoveData<Value>(t){}
//	RefData(const T& t) {
//		MoveData<Value>::SetRefValue(t);
//	}
//};
};
#endif

