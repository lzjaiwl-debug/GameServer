#pragma once
namespace AccountServer {
	struct UuidHash {
		size_t operator()(const Uuid& u) const noexcept {
			return static_cast<size_t>(u.GetHashValue());
		}
	};

	struct QueueData : Moveable<QueueData>
	{
		AssetLib::BinaryData data;
		QueueData() = default;
		QueueData(AssetLib::BinaryData&& d) : data(pick(d)) {}
		QueueData(const AssetLib::BinaryData* d) : data(*d) {}
	};
	class listenBase
	{
		typedef listenBase CLASSNAME;
	public:
		listenBase() {};
		~listenBase() {};
		void SetTimeAsync(AssetLib::TimeEventAsync<void, SharedPtr<TIMETASK_BASE>>* timeAsync) {
			this->timeAsync = timeAsync;
		}
	protected:
		NetLibrary::njson config_;
		NetLibrary::NetInterface* net_interface_ = nullptr;
		AssetLib::ThreadQueue<QUEUE_BASE, void> queue_;
		AssetLib::ConcurrentMap<Uuid, SharedPtr<USER_INFO>, UuidHash> client_map_;
		AssetLib::TimeEventAsync<void, SharedPtr<TIMETASK_BASE>>* timeAsync = nullptr;
		//Event<mysqlProto::QueryType, const String&, const Vector<Value>&, Event<Value>> SqlQueryEv;// 查询事件，参数：查询类型、SQL语句、参数列表、回调函数
		//Event<const String&, const Vector<Value>&, const Vector<String>&, Event<Value>> SqlStorageEv;// 存储过程事件，参数：存储过程名、输入参数列表、输出参数名列表、回调函数
	protected:
		virtual void ProcessQueue(QUEUE_BASE* data) = 0;
		template <typename T>
		void HandleResponse(const String& sid, int ackid, NetLibrary::EMIEVENT ev, bool issuccess, uint32_t code, const char* msg, T& res)

		{
			res.ev = ev;
			res.success = issuccess;
			res.code = code;
			res.message = msg;
			HandleResponse(sid, ackid, res);
		}
		
		template <typename T>
		void HandleResponse(const String& sid, int ackid, T& res)
		{
			AssetLib::BinaryData data;
			NetLibrary::SockData sd;
			sd.ackId = ackid;
			sd.sid = sid;
			sd.isBinary = true;
			AssetLib::SerializerFactory::Serialize(res, sd.data);
			AssetLib::SerializerFactory::Serialize(sd, data);
			net_interface_->Emit(EVT_BASE::RESPONSE, data, true);
		}
		void StartNetServer(const NetLibrary::njson& config);
		void StopNetServer();
	private:
		virtual void ReceiveMessages(const AssetLib::EventArgs& args) = 0;
		virtual void HandleClientConnect(const AssetLib::BinaryData& data);
		virtual void HandleClientDisconnect(const AssetLib::BinaryData& data);
		virtual void HandleSystemError(const AssetLib::BinaryData& data);
		virtual void HandleSystemLog(const AssetLib::BinaryData& data);
	};
}