cmake_minimum_required(VERSION 3.10)
project(AdminCertification VERSION 1.0 LANGUAGES C CXX)

# 收集源文件与头文件
file(GLOB_RECURSE ADMIN_HEADER CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/AdminCertification/*.h")
file(GLOB_RECURSE ADMIN_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/AdminCertification/*.cpp")

# 创建可执行文件
add_executable(AdminCertification WIN32 ${ADMIN_SOURCES} ${ADMIN_HEADER})

# 设置 C++ 标准
if(TARGET base-interface)
	target_link_libraries(AdminCertification PUBLIC base-interface)
	if(COMMAND base_interface_export_all_headers)
		base_interface_export_all_headers(AdminCertification)
	endif()
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "base-interface target not found. AdminCertification may not have all necessary configurations.")
	return()
endif()

include(${CMAKE_SOURCE_DIR}/third-party/cmake/upp.cmake)

# Build required U++ components with a unique prefix to avoid global cache pollution
upp_build(acf PLUGIN_Z CORE SSL DRAW BMP PNG JPEG PDFDRAW RICHTEXT PAINTER CTRLCORE CTRLLIB GRIDLAY GRIDCTRL TERMINALCTRL OUT_INTERFACE acf_upp_iface)

if(TARGET ${acf_upp_iface})
    target_link_libraries(AdminCertification PRIVATE ${acf_upp_iface})
else()
    message(CMAKE_COMPILE_WARNING_AS_ERROR "${acf_upp_iface} target not found. AdminCertification may not have all necessary configurations.")
    return()
endif()

# AdminCertification 直接调用 OpenSSL 原生 API，需显式链接 openssl-interface
if(TARGET openssl-interface)
	target_link_libraries(AdminCertification PRIVATE openssl-interface)
else()
	message(WARNING "openssl-interface not found; AdminCertification OpenSSL calls may fail to compile")
endif()

if(WIN32)
	target_link_libraries(AdminCertification PRIVATE mpr)
endif()

set_target_properties(AdminCertification PROPERTIES
	OUTPUT_NAME "AdminCertification"
	VERSION "1.0.0"
	SOVERSION 1
	POSITION_INDEPENDENT_CODE ON
)