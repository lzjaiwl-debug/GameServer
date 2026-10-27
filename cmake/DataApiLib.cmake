cmake_minimum_required(VERSION 3.10)
project(DataApiLib VERSION 1.0 LANGUAGES C CXX)

file(GLOB_RECURSE DATAAPILIB_HEADER CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/DataApiLib/*.h" "${CMAKE_SOURCE_DIR}/DataApiLib/*.hpp")
file(GLOB_RECURSE DATAAPILIB_SOURCES CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/DataApiLib/*.cpp")

add_library(DataApiLib SHARED ${DATAAPILIB_SOURCES} ${DATAAPILIB_HEADER})

if(TARGET base-interface)
    target_link_libraries(DataApiLib PUBLIC base-interface)
    if(COMMAND base_interface_export_all_headers)
        base_interface_export_all_headers(DataApiLib)
    endif()
else()
    message(CMAKE_COMPILE_WARNING_AS_ERROR "base-interface target not found. DataApiLib may not have all necessary configurations.")
    return()
endif()

include(${CMAKE_SOURCE_DIR}/third-party/cmake/upp.cmake)

upp_build(dataapi PLUGIN_Z CORE PCRE ESC SQL MYSQL OUT_INTERFACE dataapi_upp_iface)
if(TARGET ${dataapi_upp_iface})
    target_link_libraries(DataApiLib PRIVATE ${dataapi_upp_iface})
else()
    message(CMAKE_COMPILE_WARNING_AS_ERROR "${dataapi_upp_iface} target not found. DataApiLib may not have all necessary configurations.")
    return()
endif()
if(TARGET mysql-interface)
    target_link_libraries(DataApiLib PRIVATE mysql-interface)
else()
    message(CMAKE_COMPILE_WARNING_AS_ERROR "mysql-interface target not found. DataApiLib may not have all necessary configurations.")
    return()
endif()
if(TARGET dataapi-upp-pcre)
    target_compile_definitions(dataapi-upp-pcre PRIVATE HAVE_CONFIG_H)
endif()
if (WIN32)
  target_compile_definitions(DataApiLib PRIVATE APILIB_EXPORTS)
endif()
target_compile_definitions(DataApiLib PRIVATE CTRLEXTREF="${CMAKE_SOURCE_DIR}/DataApiLib/inc/ApiLibrary.h")
set_target_properties(DataApiLib PROPERTIES
    OUTPUT_NAME "DataApiLib"
    VERSION "1.0.0"
    SOVERSION 1
    POSITION_INDEPENDENT_CODE ON
)