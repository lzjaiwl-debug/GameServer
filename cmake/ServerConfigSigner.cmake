cmake_minimum_required(VERSION 3.10)
project(ServerConfigSigner VERSION 1.0 LANGUAGES C CXX)

file(GLOB_RECURSE SERVER_CONFIG_SIGNER_HEADER CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ServerConfigSigner/*.h" "${CMAKE_SOURCE_DIR}/ServerConfigSigner/*.hpp")
file(GLOB_RECURSE SERVER_CONFIG_SIGNER_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/ServerConfigSigner/*.cpp")

add_executable(ServerConfigSigner ${SERVER_CONFIG_SIGNER_SOURCES} ${SERVER_CONFIG_SIGNER_HEADER})

if(TARGET base-interface)
	target_link_libraries(ServerConfigSigner PUBLIC base-interface)
	if(COMMAND base_interface_export_all_headers)
		base_interface_export_all_headers(ServerConfigSigner)
	endif()
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "base-interface target not found. ServerConfigSigner may not have all necessary configurations.")
	return()
endif()

include(${CMAKE_SOURCE_DIR}/third-party/cmake/upp.cmake)

upp_build(scs PLUGIN_Z CORE SSL OUT_INTERFACE scs_upp_iface)
if(TARGET ${scs_upp_iface})
	target_link_libraries(ServerConfigSigner PRIVATE ${scs_upp_iface})
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "${scs_upp_iface} target not found. ServerConfigSigner may not have all necessary configurations.")
	return()
endif()

if(TARGET openssl-interface)
	target_link_libraries(ServerConfigSigner PRIVATE openssl-interface)
else()
	message(WARNING "openssl-interface not found; ServerConfigSigner OpenSSL calls may fail to compile")
endif()
if(WIN32)
    target_link_libraries(ServerConfigSigner PRIVATE Ws2_32 winmm crypt32 advapi32)
endif()
set_target_properties(ServerConfigSigner PROPERTIES
	OUTPUT_NAME "ServerConfigSigner"
	VERSION "1.0.0"
	SOVERSION 1
	POSITION_INDEPENDENT_CODE ON
)
