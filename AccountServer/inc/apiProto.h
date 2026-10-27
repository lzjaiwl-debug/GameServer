#pragma once
namespace AccountServer {
	namespace apiProto {

		struct API_EXECUTE_REQ : public REQ_BASE
		{
			std::string module;
			std::string type;
			std::string payload;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% module% type% payload;
			}
		};
		struct API_EXECUTE_RES : public RES_BASE
		{
			std::string payload;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				RES_BASE::Serialize(s);
				s% payload;
			}
		};
		struct API_REQUEST : public REQ_BASE
		{
			std::string module;
			std::string type;
			std::string payload;
			void Serialize(AssetLib::SerializerFactory& s)
			{
				REQ_BASE::Serialize(s);
				s% module% type% payload;
			}
		};

	}
}
