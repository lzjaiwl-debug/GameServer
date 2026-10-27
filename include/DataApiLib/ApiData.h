#pragma once
namespace ApiLibrary {
	struct ApiData
	{
		std::string funname;
		std::string args;
		void Serialize(AssetLib::SerializerFactory& s)
		{
			s% funname% args;
		}
	};
};