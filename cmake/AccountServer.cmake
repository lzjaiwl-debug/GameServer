cmake_minimum_required(VERSION 3.10)
project(AccountServer VERSION 1.0 LANGUAGES C CXX)

# 收集源文件与头文件
file(GLOB_RECURSE ACCOUNT_HEADER CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/AccountServer/*.h" "${CMAKE_SOURCE_DIR}/AccountServer/*.hpp")
file(GLOB_RECURSE ACCOUNT_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/AccountServer/*.cpp")

# 创建可执行文件
add_executable(AccountServer ${ACCOUNT_SOURCES} ${ACCOUNT_HEADER})

# 设置 C++ 标准
if(TARGET base-interface)
	target_link_libraries(AccountServer PUBLIC base-interface)
	if(COMMAND base_interface_export_all_headers)
		base_interface_export_all_headers(AccountServer)
	endif()
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "base-interface target not found. AccountServer may not have all necessary configurations.")
	return()
endif()

include(${CMAKE_SOURCE_DIR}/third-party/cmake/upp.cmake)
# Build required U++ components with a unique prefix to avoid global cache pollution
upp_build(acs PLUGIN_Z CORE SSL SQL MYSQL OUT_INTERFACE acs_upp_iface)
if(TARGET ${acs_upp_iface})
    target_link_libraries(AccountServer PRIVATE ${acs_upp_iface})
else()
    message(CMAKE_COMPILE_WARNING_AS_ERROR "${acs_upp_iface} target not found. NetLibrary may not have all necessary configurations.")
    return()
endif()

# 
if(TARGET NetLibrary)
	target_link_libraries(AccountServer PRIVATE NetLibrary)
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "NetLibrary target not found. AccountServer may not have all necessary configurations.")
	return()
endif()

if(TARGET DataApiLib)
	target_link_libraries(AccountServer PRIVATE DataApiLib)
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "DataApiLib target not found. AccountServer may not have all necessary configurations.")
	return()
endif()

# AccountServer 直接使用 OpenSSL 原生 API（PasswordHash.cpp：Argon2id / PBKDF2），
# 需显式链接 openssl-interface；U++ 内部目标是 PRIVATE 链接，不会传递给本目标
if(TARGET openssl-interface)
	target_link_libraries(AccountServer PRIVATE openssl-interface)
else()
	message(WARNING "openssl-interface not found; PasswordHash.cpp will fail to compile")
endif()
if(TARGET mysql-interface)
    target_link_libraries(AccountServer PRIVATE mysql-interface)
else()
    message(CMAKE_COMPILE_WARNING_AS_ERROR "mysql-interface target not found. AccountServer may not have all necessary configurations.")
    return()
endif()
if(WIN32)
    target_link_libraries(AccountServer PRIVATE Ws2_32 winmm crypt32 advapi32)
	# 运行时复制 DLL 到可执行输出目录，避免找不到 DataApiLib.dll 和 NetLibrary.dll
	add_custom_command(TARGET AccountServer POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:NetLibrary>
				$<TARGET_FILE_DIR:AccountServer>
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
				$<TARGET_FILE:DataApiLib>
				$<TARGET_FILE_DIR:AccountServer>
	)
endif()

set_target_properties(AccountServer PROPERTIES
	OUTPUT_NAME "AccountServer"
	VERSION "1.0.0"
	SOVERSION 1
	POSITION_INDEPENDENT_CODE ON
)