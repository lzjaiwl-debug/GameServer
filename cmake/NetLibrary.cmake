cmake_minimum_required(VERSION 3.10)
project(NetLibrary VERSION 1.0 LANGUAGES C CXX)

file(GLOB_RECURSE NETLIBRARY_HEADER CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/NetLibrary/inc/*.h" "${CMAKE_SOURCE_DIR}/NetLibrary/inc/*.hpp")
list(REMOVE_ITEM NETLIBRARY_HEADER "${CMAKE_SOURCE_DIR}/NetLibrary/inc/PoolLib.hpp")
file(GLOB_RECURSE NETLIBRARY_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/NetLibrary/src/*.cpp")
add_library(NetLibrary SHARED ${NETLIBRARY_SOURCES} ${NETLIBRARY_HEADER})

if(TARGET base-interface)
    target_link_libraries(NetLibrary PRIVATE base-interface)
    if(COMMAND base_interface_export_all_headers)
        base_interface_export_all_headers(NetLibrary)
    endif()
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "base-interface target not found. NetLibrary may not have all necessary configurations.")
	return()
endif()
include(${CMAKE_SOURCE_DIR}/third-party/cmake/upp.cmake)

# Build required U++ components with a unique prefix to avoid global cache pollution
upp_build(net PLUGIN_Z CORE SSL OUT_INTERFACE net_upp_iface)
if(TARGET ${net_upp_iface})
    target_link_libraries(NetLibrary PRIVATE ${net_upp_iface})
else()
    message(CMAKE_COMPILE_WARNING_AS_ERROR "${net_upp_iface} target not found. NetLibrary may not have all necessary configurations.")
    return()
endif()

# Keep explicit OpenSSL link as before (provided by openssl.cmake)
if(TARGET openssl-interface)
	target_link_libraries(NetLibrary PRIVATE openssl-interface)
else()
	message(CMAKE_COMPILE_WARNING_AS_ERROR "openssl-interface target not found. NetLibrary may not have all necessary configurations.")
	return()
endif()

if(WIN32)
	target_compile_definitions(NetLibrary PRIVATE NETLIB_EXPORTS)
endif()

set_target_properties(NetLibrary PROPERTIES
	OUTPUT_NAME "NetLibrary"
	VERSION ${PROJECT_VERSION}
	SOVERSION 1
	POSITION_INDEPENDENT_CODE ON
)