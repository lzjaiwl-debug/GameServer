#include "../AccountServer.h"

namespace AccountServer {

	// ServerConfig 的全局实例定义。
	// 之所以单独放一个 .cpp：ServerConfig.h 是纯头文件且被多处包含，
	// 若把定义写在头文件里，每个 TU 都会拿到一份实例（C++17 前）或造成重复符号。
	ServerConfig g_cfg;
	AssetLib::EventBus<> assetEventBus;
}
