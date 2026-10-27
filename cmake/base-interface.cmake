# 1. 创建接口库来管理公共配置
if(NOT TARGET base-interface)
    add_library(base-interface INTERFACE)
endif()
# 2. 设置公共包含目录（所有链接此接口的目标都会获得）
function(base_interface_export_all_headers consumer_target)
    if(NOT TARGET ${consumer_target})
        message(FATAL_ERROR "base_interface_export_all_headers(): target not found: ${consumer_target}")
    endif()

    set(_base_interface_headers_root "${CMAKE_SOURCE_DIR}/include")
    if(NOT IS_DIRECTORY "${_base_interface_headers_root}")
        message(STATUS "[base-interface] include dir not found: ${_base_interface_headers_root} (skip exporting headers)")
        return()
    endif()

    file(GLOB_RECURSE _base_interface_headers CONFIGURE_DEPENDS "${_base_interface_headers_root}/*.*")
    if(_base_interface_headers)
        target_sources(${consumer_target} PRIVATE ${_base_interface_headers})
        target_include_directories(${consumer_target} PRIVATE ${_base_interface_headers_root})
        source_group(TREE "${_base_interface_headers_root}" PREFIX "include" FILES ${_base_interface_headers})
    endif()
endfunction()
target_include_directories(base-interface INTERFACE ${CMAKE_SOURCE_DIR}/include)
# 添加项目公共编译定义（仅 Debug）
target_compile_definitions(base-interface INTERFACE $<$<CONFIG:Debug>:flagDEBUG>)
# Debug 下启用 AddressSanitizer（MSVC）
# 3. 设置公共编译特性
target_compile_features(base-interface INTERFACE cxx_std_17)
# 4. 设置平台特定的链接库
if(LINUX AND UNIX)
    target_link_libraries(base-interface INTERFACE pthread dl rt)
        # GCC 特定选项（来自 .upp 的 link(GCC POSIX STACKTRACE) -rdynamic）
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(base-interface INTERFACE -rdynamic)
    endif()
endif()
if(WIN32)
    target_link_libraries(base-interface INTERFACE winmm crypt32 usp10 user32 gdi32 advapi32 comdlg32 comctl32 imm32)
endif()