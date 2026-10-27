#pragma once

#ifdef _WIN32
    #ifndef WIN32_LEAN_AND_MEAN
    #define WIN32_LEAN_AND_MEAN
    #endif
    // πÿº¸£∫◊Ë÷π Windows.h ∂®“Â min/max ∫Í£¨±‹√‚”Î std::min/std::max ≥ÂÕª
    #ifndef NOMINMAX
    #define NOMINMAX
    #endif
    #ifndef _WINSOCKAPI_
    #define _WINSOCKAPI_
    #endif
    #include <windows.h>
    #include <winreg.h>
#else
#include <unistd.h>
#endif
#include <ostream>
#include <iostream>
#include <stdexcept>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <functional>
#include <string>
#include <any>
#include <mutex>
#include <shared_mutex>
#include <atomic>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <utility>
#include <condition_variable>
#include <chrono>
#include <deque>
#include <cassert>
#include <string_view>
#include <array>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <fstream>
#include <sstream>

#include <nlohmann/json.hpp>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include "AssetPool.hpp"
#include "ThreadWork.hpp"
#include "ThreadQueue.hpp"
#include "ConcurrentMap.hpp"
#include "TimeEventAsync.hpp"
#include "SerializerFactory.hpp"
#include "EventBus.hpp"
#include "MemManager.hpp"
#include "ServerConfigLicense.hpp"
