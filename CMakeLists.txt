# CMakeList.txt: GameServer 的 CMake 项目，在此处包括源代码并定义
# 项目特定的逻辑。
#
cmake_minimum_required (VERSION 3.10)

# Honor CMAKE_MSVC_RUNTIME_LIBRARY for selecting /MT,/MD runtime on MSVC.
if(POLICY CMP0091)
  cmake_policy(SET CMP0091 NEW)
endif()

# 如果支持，请为 MSVC 编译器启用热重载。
if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:EditAndContinue>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()

project (GameServer VERSION 1.0 LANGUAGES C CXX)

#创建接口库来管理公共配置
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/base-interface.cmake")

# 载入NetLibrary入口
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/NetLibrary.cmake")

# 载入DataApiLib入口
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/DataApiLib.cmake")

# 载入AdminCertification入口
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AdminCertification.cmake")

# 载入ServerConfigSigner入口
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/ServerConfigSigner.cmake")

# 载入AccountServer入口
include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/AccountServer.cmake")

# TODO: 如有需要，请添加测试并安装目标。
