include_guard(GLOBAL)

set(UPP_ROOT "${CMAKE_SOURCE_DIR}/third-party/uppsrc" CACHE PATH "U++ source root")

if(NOT IS_DIRECTORY "${UPP_ROOT}")
    message(FATAL_ERROR "U++ root not found: ${UPP_ROOT}")
endif()

function(_upp_resolve_out_dir out_var)
    if(WIN32)
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            set(_d "${CMAKE_SOURCE_DIR}/third-party/lib/win-x64")
        else()
            set(_d "${CMAKE_SOURCE_DIR}/third-party/lib/win-x86")
        endif()
    else()
        set(_d "${CMAKE_SOURCE_DIR}/third-party/lib/linux")
    endif()
    file(MAKE_DIRECTORY "${_d}")
    set(${out_var} "${_d}" PARENT_SCOPE)
endfunction()

function(_upp_apply_output_dirs target out_dir)
    set_target_properties(${target} PROPERTIES
        ARCHIVE_OUTPUT_DIRECTORY "${out_dir}"
        LIBRARY_OUTPUT_DIRECTORY "${out_dir}"
        RUNTIME_OUTPUT_DIRECTORY "${out_dir}"
        ARCHIVE_OUTPUT_DIRECTORY_DEBUG "${out_dir}"
        ARCHIVE_OUTPUT_DIRECTORY_RELEASE "${out_dir}"
        ARCHIVE_OUTPUT_DIRECTORY_RELWITHDEBINFO "${out_dir}"
        ARCHIVE_OUTPUT_DIRECTORY_MINSIZEREL "${out_dir}"
        LIBRARY_OUTPUT_DIRECTORY_DEBUG "${out_dir}"
        LIBRARY_OUTPUT_DIRECTORY_RELEASE "${out_dir}"
        LIBRARY_OUTPUT_DIRECTORY_RELWITHDEBINFO "${out_dir}"
        LIBRARY_OUTPUT_DIRECTORY_MINSIZEREL "${out_dir}"
        RUNTIME_OUTPUT_DIRECTORY_DEBUG "${out_dir}"
        RUNTIME_OUTPUT_DIRECTORY_RELEASE "${out_dir}"
        RUNTIME_OUTPUT_DIRECTORY_RELWITHDEBINFO "${out_dir}"
        RUNTIME_OUTPUT_DIRECTORY_MINSIZEREL "${out_dir}"
    )
endfunction()

function(upp_build prefix)
    set(options
        PLUGIN_Z CORE SSL SSH PCRE ESC SQL MYSQL
        DRAW BMP JPEG PNG PDFDRAW RICHTEXT RICHEDIT PAINTER CTRLCORE CTRLLIB GRIDLAY GRIDCTRL TERMINALCTRL
    )
    cmake_parse_arguments(UPP "" "OUT_INTERFACE" "" ${ARGN})

    if(prefix STREQUAL "")
        message(FATAL_ERROR "upp_build: prefix is required")
    endif()

    # When building multiple prefixed U++ variants in the same build tree,
    # each static library must produce a unique output file name. Otherwise
    # Ninja will error with: "multiple rules generate .../z.lib".
    set(_out_suffix "${prefix}")
    if(prefix STREQUAL "_legacy")
        set(_out_suffix "")
    endif()

    # Add build-type suffix to output names to prevent Debug/Release artifacts
    # from overwriting each other when sharing the same output directory.
    set(_cfg_suffix "")
    if(CMAKE_BUILD_TYPE MATCHES "^[Dd]ebug$")
        set(_cfg_suffix "d")
    endif()

    _upp_resolve_out_dir(_upp_out_dir)

    set(_iface "${prefix}-upp-interface")
    if(NOT TARGET ${_iface})
        add_library(${_iface} INTERFACE)
        target_include_directories(${_iface} INTERFACE
            "${UPP_ROOT}"
            "${UPP_ROOT}/plugin/z/lib"
        )
        if(WIN32 AND MSVC)
            target_compile_options(${_iface} INTERFACE /source-charset:utf-8 /execution-charset:utf-8)
        endif()
    endif()

    # Ensure common third-party interface targets exist when components are enabled
    include(${CMAKE_SOURCE_DIR}/third-party/cmake/openssl.cmake)
    include(${CMAKE_SOURCE_DIR}/third-party/cmake/mysql.cmake)

    function(_upp_add_static_prefixed out_target logical_name)
        set(_t "${prefix}-upp-${logical_name}")
        if(TARGET ${_t})
            set(${out_target} "${_t}" PARENT_SCOPE)
            return()
        endif()
        add_library(${_t} STATIC ${ARGN})

        # Keep MSVC runtime library consistent with the preset (e.g. /MT vs /MTd).
        # Requires CMP0091=NEW at top-level to fully honor CMAKE_MSVC_RUNTIME_LIBRARY.
        if(WIN32 AND MSVC AND DEFINED CMAKE_MSVC_RUNTIME_LIBRARY AND NOT CMAKE_MSVC_RUNTIME_LIBRARY STREQUAL "")
            set_property(TARGET ${_t} PROPERTY MSVC_RUNTIME_LIBRARY "${CMAKE_MSVC_RUNTIME_LIBRARY}")
        endif()
        set_target_properties(${_t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
        _upp_apply_output_dirs(${_t} "${_upp_out_dir}")
        if(TARGET base-interface)
            target_link_libraries(${_t} PRIVATE base-interface)
        endif()
        target_link_libraries(${_t} PRIVATE ${_iface})
        set(${out_target} "${_t}" PARENT_SCOPE)
    endfunction()

    function(_upp_iface_link)
        foreach(_t IN LISTS ARGN)
            if(TARGET ${_t})
                target_link_libraries(${_iface} INTERFACE ${_t})
            endif()
        endforeach()
    endfunction()

    # ------------------------------
    # plugin/z (zlib)
    # ------------------------------
    list(FIND ARGN "PLUGIN_Z" _has_plugin_z)
    if(_has_plugin_z GREATER -1)
        file(GLOB_RECURSE PLUGIN_Z_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/plugin/z/*.h" "${UPP_ROOT}/plugin/z/*.hpp")
        file(GLOB_RECURSE PLUGIN_Z_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/plugin/z/*.c")
        _upp_add_static_prefixed(_z plugin-z ${PLUGIN_Z_SOURCES} ${PLUGIN_Z_HEADER})
        if(_out_suffix STREQUAL "")
            set_target_properties(${_z} PROPERTIES OUTPUT_NAME "z${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_z} PROPERTIES OUTPUT_NAME "z-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        target_include_directories(${_z} PUBLIC "${UPP_ROOT}/plugin/z/lib")
        _upp_iface_link(${_z})
    endif()

    # ------------------------------
    # Core
    # ------------------------------
    list(FIND ARGN "CORE" _has_core)
    if(_has_core GREATER -1)
        file(GLOB_RECURSE UPP_CORE_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/Core/*.h" "${UPP_ROOT}/Core/lib/*.h" "${UPP_ROOT}/Core/*.hpp")
        file(GLOB_RECURSE UPP_CORE_SOURCES CONFIGURE_DEPENDS
            "${UPP_ROOT}/Core/*.c" "${UPP_ROOT}/Core/lib/*.c"
            "${UPP_ROOT}/Core/*.cpp" "${UPP_ROOT}/Core/*.cxx" "${UPP_ROOT}/Core/*.cc" "${UPP_ROOT}/Core/*.icpp"
        )
        list(REMOVE_ITEM UPP_CORE_SOURCES "${UPP_ROOT}/Core/StrUtil.cpp")
        _upp_add_static_prefixed(_core core ${UPP_CORE_SOURCES} ${UPP_CORE_HEADER})
        if(_out_suffix STREQUAL "")
            set_target_properties(${_core} PROPERTIES OUTPUT_NAME "Core${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_core} PROPERTIES OUTPUT_NAME "Core-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        target_link_libraries(${_core} PRIVATE openssl-interface)
        _upp_iface_link(${_core})
    endif()
    # ------------------------------
    # plugin/pcre (PCRE2)
    # ------------------------------
    list(FIND ARGN "PCRE" _has_pcre)
    if(_has_pcre GREATER -1)
        file(GLOB_RECURSE PLUGIN_PCRE_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/plugin/pcre/*.h" "${UPP_ROOT}/plugin/pcre/*.hpp")
        file(GLOB_RECURSE PLUGIN_PCRE_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/plugin/pcre/*.c" "${UPP_ROOT}/plugin/pcre/*.cpp" "${UPP_ROOT}/plugin/pcre/*.cxx" "${UPP_ROOT}/plugin/pcre/*.cc" "${UPP_ROOT}/plugin/pcre/*.icpp")
        _upp_add_static_prefixed(_pcre pcre ${PLUGIN_PCRE_SOURCES} ${PLUGIN_PCRE_HEADER})
        if(_out_suffix STREQUAL "")
            set_target_properties(${_pcre} PROPERTIES OUTPUT_NAME "pcre2${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_pcre} PROPERTIES OUTPUT_NAME "pcre2-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        target_include_directories(${_pcre} PUBLIC "${UPP_ROOT}/plugin/pcre/lib")
        _upp_iface_link(${_pcre})
    endif()
    # ------------------------------
    # Core/SSL
    # ------------------------------
    list(FIND ARGN "SSL" _has_ssl)
    if(_has_ssl GREATER -1)
        file(GLOB_RECURSE UPP_SSL_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/Core/SSL/*.h" "${UPP_ROOT}/Core/SSL/*.hpp")
        file(GLOB_RECURSE UPP_SSL_SOURCES CONFIGURE_DEPENDS
            "${UPP_ROOT}/Core/SSL/*.c" "${UPP_ROOT}/Core/SSL/*.cpp" "${UPP_ROOT}/Core/SSL/*.cxx" "${UPP_ROOT}/Core/SSL/*.cc" "${UPP_ROOT}/Core/SSL/*.icpp"
        )
        _upp_add_static_prefixed(_ssl ssl ${UPP_SSL_SOURCES} ${UPP_SSL_HEADER})
        if(_out_suffix STREQUAL "")
            set_target_properties(${_ssl} PROPERTIES OUTPUT_NAME "CoreSSL${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_ssl} PROPERTIES OUTPUT_NAME "CoreSSL-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        target_link_libraries(${_ssl} PRIVATE openssl-interface)
        _upp_iface_link(${_ssl})
    endif()

    # ------------------------------
    # Core/SSH
    # ------------------------------
    list(FIND ARGN "SSH" _has_ssh)
    if(_has_ssh GREATER -1)
        file(GLOB_RECURSE UPP_SSH_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/Core/SSH/*.h" "${UPP_ROOT}/Core/SSH/*.hpp")
        file(GLOB_RECURSE UPP_SSH_SOURCES CONFIGURE_DEPENDS
            "${UPP_ROOT}/Core/SSH/*.c" "${UPP_ROOT}/Core/SSH/*.cpp" "${UPP_ROOT}/Core/SSH/*.cxx" "${UPP_ROOT}/Core/SSH/*.cc" "${UPP_ROOT}/Core/SSH/*.icpp"
        )
        _upp_add_static_prefixed(_ssh ssh ${UPP_SSH_SOURCES} ${UPP_SSH_HEADER} "${UPP_ROOT}/Core/SSH/libssh2/libssh2_config.h")
        if(_out_suffix STREQUAL "")
            set_target_properties(${_ssh} PROPERTIES OUTPUT_NAME "CoreSSH${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_ssh} PROPERTIES OUTPUT_NAME "CoreSSH-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        target_link_libraries(${_ssh} PRIVATE openssl-interface)
        _upp_iface_link(${_ssh})
    endif()

    # ------------------------------
    # Esc
    # ------------------------------
    list(FIND ARGN "ESC" _has_esc)
    if(_has_esc GREATER -1)
        file(GLOB_RECURSE UPP_ESC_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/Esc/*.h" "${UPP_ROOT}/Esc/*.hpp")
        file(GLOB_RECURSE UPP_ESC_SOURCES CONFIGURE_DEPENDS
            "${UPP_ROOT}/Esc/*.c" "${UPP_ROOT}/Esc/*.cpp" "${UPP_ROOT}/Esc/*.cxx" "${UPP_ROOT}/Esc/*.cc" "${UPP_ROOT}/Esc/*.icpp"
        )
        _upp_add_static_prefixed(_esc esc ${UPP_ESC_SOURCES} ${UPP_ESC_HEADER})
        if(_out_suffix STREQUAL "")
            set_target_properties(${_esc} PROPERTIES OUTPUT_NAME "CoreEsc${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_esc} PROPERTIES OUTPUT_NAME "CoreEsc-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        _upp_iface_link(${_esc})
    endif()

    # ------------------------------
    # Sql
    # ------------------------------
    list(FIND ARGN "SQL" _has_sql)
    if(_has_sql GREATER -1)
        file(GLOB_RECURSE UPP_SQL_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/Sql/*.h" "${UPP_ROOT}/Sql/*.hpp")
        file(GLOB_RECURSE UPP_SQL_SOURCES CONFIGURE_DEPENDS
            "${UPP_ROOT}/Sql/*.c" "${UPP_ROOT}/Sql/*.cpp" "${UPP_ROOT}/Sql/*.cxx" "${UPP_ROOT}/Sql/*.cc" "${UPP_ROOT}/Sql/*.icpp"
        )
        _upp_add_static_prefixed(_sql sql ${UPP_SQL_SOURCES} ${UPP_SQL_HEADER})
        if(_out_suffix STREQUAL "")
            set_target_properties(${_sql} PROPERTIES OUTPUT_NAME "CoreSql${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_sql} PROPERTIES OUTPUT_NAME "CoreSql-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        _upp_iface_link(${_sql})
    endif()

    # ------------------------------
    # MySql
    # ------------------------------
    list(FIND ARGN "MYSQL" _has_mysql)
    if(_has_mysql GREATER -1)
        file(GLOB_RECURSE UPP_MYSQL_HEADER CONFIGURE_DEPENDS "${UPP_ROOT}/MySql/*.h" "${UPP_ROOT}/MySql/*.hpp")
        file(GLOB_RECURSE UPP_MYSQL_SOURCES CONFIGURE_DEPENDS
            "${UPP_ROOT}/MySql/*.c" "${UPP_ROOT}/MySql/*.cpp" "${UPP_ROOT}/MySql/*.cxx" "${UPP_ROOT}/MySql/*.cc" "${UPP_ROOT}/MySql/*.icpp"
        )
        _upp_add_static_prefixed(_mysql mysql ${UPP_MYSQL_SOURCES} ${UPP_MYSQL_HEADER})
        if(_out_suffix STREQUAL "")
            set_target_properties(${_mysql} PROPERTIES OUTPUT_NAME "CoreMySql${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        else()
            set_target_properties(${_mysql} PROPERTIES OUTPUT_NAME "CoreMySql-${_out_suffix}${_cfg_suffix}" POSITION_INDEPENDENT_CODE ON)
        endif()
        target_link_libraries(${_mysql} PRIVATE mysql-interface)
        target_link_libraries(${_mysql} PRIVATE openssl-interface)

        if(WIN32)
            get_target_property(_ossl_libdirs openssl-interface INTERFACE_LINK_DIRECTORIES)
            message(STATUS "[upp] ${_mysql}: openssl-interface link dirs = ${_ossl_libdirs}")
        endif()

        _upp_iface_link(${_mysql})
    endif()

    if(WIN32)
        list(FIND ARGN "DRAW" _has_draw)
        if(_has_draw GREATER -1)
            file(GLOB_RECURSE UPP_DRAW_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/Draw/*.c" "${UPP_ROOT}/Draw/*.cpp" "${UPP_ROOT}/Draw/*.cxx" "${UPP_ROOT}/Draw/*.cc" "${UPP_ROOT}/Draw/*.icpp" "${UPP_ROOT}/Draw/*.hpp")
            _upp_add_static_prefixed(_draw draw ${UPP_DRAW_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_draw} PROPERTIES OUTPUT_NAME "CoreDraw" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_draw} PROPERTIES OUTPUT_NAME "CoreDraw-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_draw})
        endif()

        list(FIND ARGN "BMP" _has_bmp)
        if(_has_bmp GREATER -1)
            file(GLOB_RECURSE UPP_BMP_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/plugin/bmp/*.c" "${UPP_ROOT}/plugin/bmp/*.cpp" "${UPP_ROOT}/plugin/bmp/*.cxx" "${UPP_ROOT}/plugin/bmp/*.cc" "${UPP_ROOT}/plugin/bmp/*.icpp" "${UPP_ROOT}/plugin/bmp/*.hpp")
            _upp_add_static_prefixed(_bmp bmp ${UPP_BMP_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_bmp} PROPERTIES OUTPUT_NAME "CoreBmp" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_bmp} PROPERTIES OUTPUT_NAME "CoreBmp-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_bmp})
        endif()

        list(FIND ARGN "JPEG" _has_jpeg)
        if(_has_jpeg GREATER -1)
            file(GLOB_RECURSE UPP_JPEG_SOURCES CONFIGURE_DEPENDS
                "${UPP_ROOT}/plugin/jpg/*.c" "${UPP_ROOT}/plugin/jpg/*.cpp" "${UPP_ROOT}/plugin/jpg/*.cxx" "${UPP_ROOT}/plugin/jpg/*.cc" "${UPP_ROOT}/plugin/jpg/*.icpp" "${UPP_ROOT}/plugin/jpg/*.hpp"
                "${UPP_ROOT}/plugin/jpg/lib/*.c" "${UPP_ROOT}/plugin/jpg/lib/*.cpp" "${UPP_ROOT}/plugin/jpg/lib/*.cxx" "${UPP_ROOT}/plugin/jpg/lib/*.cc" "${UPP_ROOT}/plugin/jpg/lib/*.icpp" "${UPP_ROOT}/plugin/jpg/lib/*.hpp"
            )
            list(REMOVE_ITEM UPP_JPEG_SOURCES
                "${UPP_ROOT}/plugin/jpg/lib/cjpegalt.c"
                "${UPP_ROOT}/plugin/jpg/lib/djpegalt.c"
                "${UPP_ROOT}/plugin/jpg/lib/cjpeg.c"
            )
            _upp_add_static_prefixed(_jpeg jpeg ${UPP_JPEG_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_jpeg} PROPERTIES OUTPUT_NAME "CoreJpeg" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_jpeg} PROPERTIES OUTPUT_NAME "CoreJpeg-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            target_include_directories(${_jpeg} PRIVATE "${UPP_ROOT}/plugin/jpg/lib")
            _upp_iface_link(${_jpeg})
        endif()

        list(FIND ARGN "PNG" _has_png)
        if(_has_png GREATER -1)
            file(GLOB_RECURSE UPP_PNG_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/plugin/png/*.c" "${UPP_ROOT}/plugin/png/*.cpp" "${UPP_ROOT}/plugin/png/*.cxx" "${UPP_ROOT}/plugin/png/*.cc" "${UPP_ROOT}/plugin/png/*.icpp" "${UPP_ROOT}/plugin/png/*.hpp")
            list(REMOVE_ITEM UPP_PNG_SOURCES
                "${UPP_ROOT}/plugin/png/pngtest.c"
            )
            _upp_add_static_prefixed(_png png ${UPP_PNG_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_png} PROPERTIES OUTPUT_NAME "CorePng" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_png} PROPERTIES OUTPUT_NAME "CorePng-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            target_include_directories(${_png} PRIVATE ${UPP_ROOT}/plugin/png/lib)
            _upp_iface_link(${_png})
        endif()

        list(FIND ARGN "PDFDRAW" _has_pdfdraw)
        if(_has_pdfdraw GREATER -1)
            file(GLOB_RECURSE UPP_PDFDRAW_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/PdfDraw/*.c" "${UPP_ROOT}/PdfDraw/*.cpp" "${UPP_ROOT}/PdfDraw/*.cxx" "${UPP_ROOT}/PdfDraw/*.cc" "${UPP_ROOT}/PdfDraw/*.icpp" "${UPP_ROOT}/PdfDraw/*.hpp")
            _upp_add_static_prefixed(_pdfdraw pdfdraw ${UPP_PDFDRAW_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_pdfdraw} PROPERTIES OUTPUT_NAME "CorePdfDraw" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_pdfdraw} PROPERTIES OUTPUT_NAME "CorePdfDraw-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_pdfdraw})
        endif()

        list(FIND ARGN "RICHTEXT" _has_richtext)
        if(_has_richtext GREATER -1)
            file(GLOB_RECURSE UPP_RICHTEXT_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/RichText/*.c" "${UPP_ROOT}/RichText/*.cpp" "${UPP_ROOT}/RichText/*.cxx" "${UPP_ROOT}/RichText/*.cc" "${UPP_ROOT}/RichText/*.icpp" "${UPP_ROOT}/RichText/*.hpp")
            _upp_add_static_prefixed(_richtext richtext ${UPP_RICHTEXT_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_richtext} PROPERTIES OUTPUT_NAME "CoreRichText" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_richtext} PROPERTIES OUTPUT_NAME "CoreRichText-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_richtext})
        endif()

        list(FIND ARGN "RICHEDIT" _has_richedit)
        if(_has_richedit GREATER -1)
            file(GLOB_RECURSE UPP_RICHEDIT_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/RichEdit/*.c" "${UPP_ROOT}/RichEdit/*.cpp" "${UPP_ROOT}/RichEdit/*.cxx" "${UPP_ROOT}/RichEdit/*.cc" "${UPP_ROOT}/RichEdit/*.icpp" "${UPP_ROOT}/RichEdit/*.hpp")
            _upp_add_static_prefixed(_richedit richedit ${UPP_RICHEDIT_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_richedit} PROPERTIES OUTPUT_NAME "CoreRichEdit" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_richedit} PROPERTIES OUTPUT_NAME "CoreRichEdit-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_richedit})
        endif()

        list(FIND ARGN "PAINTER" _has_painter)
        if(_has_painter GREATER -1)
            file(GLOB_RECURSE UPP_PAINTER_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/Painter/*.c" "${UPP_ROOT}/Painter/*.cpp" "${UPP_ROOT}/Painter/*.cxx" "${UPP_ROOT}/Painter/*.cc" "${UPP_ROOT}/Painter/*.icpp" "${UPP_ROOT}/Painter/*.hpp")
            _upp_add_static_prefixed(_painter painter ${UPP_PAINTER_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_painter} PROPERTIES OUTPUT_NAME "CorePainter" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_painter} PROPERTIES OUTPUT_NAME "CorePainter-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_painter})
        endif()

        list(FIND ARGN "CTRLCORE" _has_ctrlcore)
        if(_has_ctrlcore GREATER -1)
            file(GLOB_RECURSE UPP_CTRLCORE_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/CtrlCore/*.c" "${UPP_ROOT}/CtrlCore/*.cpp" "${UPP_ROOT}/CtrlCore/*.cxx" "${UPP_ROOT}/CtrlCore/*.cc" "${UPP_ROOT}/CtrlCore/*.icpp" "${UPP_ROOT}/CtrlCore/*.hpp")
            _upp_add_static_prefixed(_ctrlcore CtrlCore ${UPP_CTRLCORE_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_ctrlcore} PROPERTIES OUTPUT_NAME "CoreCtrlCore" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_ctrlcore} PROPERTIES OUTPUT_NAME "CoreCtrlCore-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_ctrlcore})
        endif()

        list(FIND ARGN "CTRLLIB" _has_ctrllib)
        if(_has_ctrllib GREATER -1)
            file(GLOB_RECURSE UPP_CTRLLIB_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/CtrlLib/*.c" "${UPP_ROOT}/CtrlLib/*.cpp" "${UPP_ROOT}/CtrlLib/*.cxx" "${UPP_ROOT}/CtrlLib/*.cc" "${UPP_ROOT}/CtrlLib/*.icpp" "${UPP_ROOT}/CtrlLib/*.hpp")
            _upp_add_static_prefixed(_ctrllib CtrlLib ${UPP_CTRLLIB_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_ctrllib} PROPERTIES OUTPUT_NAME "CoreCtrlLib" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_ctrllib} PROPERTIES OUTPUT_NAME "CoreCtrlLib-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_ctrllib})
        endif()

        list(FIND ARGN "GRIDLAY" _has_gridlay)
        if(_has_gridlay GREATER -1)
            file(GLOB_RECURSE UPP_GRIDLAY_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/GridLayoutCtrl/*.c" "${UPP_ROOT}/GridLayoutCtrl/*.cpp" "${UPP_ROOT}/GridLayoutCtrl/*.cxx" "${UPP_ROOT}/GridLayoutCtrl/*.cc" "${UPP_ROOT}/GridLayoutCtrl/*.icpp" "${UPP_ROOT}/GridLayoutCtrl/*.hpp")
            _upp_add_static_prefixed(_gridlay GridLay ${UPP_GRIDLAY_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_gridlay} PROPERTIES OUTPUT_NAME "CoreGridLay" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_gridlay} PROPERTIES OUTPUT_NAME "CoreGridLay-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_gridlay})
        endif()

        list(FIND ARGN "GRIDCTRL" _has_gridctrl)
        if(_has_gridctrl GREATER -1)
            file(GLOB_RECURSE UPP_GRIDCTRL_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/GridCtrl/*.c" "${UPP_ROOT}/GridCtrl/*.cpp" "${UPP_ROOT}/GridCtrl/*.cxx" "${UPP_ROOT}/GridCtrl/*.cc" "${UPP_ROOT}/GridCtrl/*.icpp" "${UPP_ROOT}/GridCtrl/*.hpp")
            _upp_add_static_prefixed(_gridctrl GridCtrl ${UPP_GRIDCTRL_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_gridctrl} PROPERTIES OUTPUT_NAME "CoreGridCtrl" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_gridctrl} PROPERTIES OUTPUT_NAME "CoreGridCtrl-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_gridctrl})
        endif()

        list(FIND ARGN "TERMINALCTRL" _has_terminalctrl)
        if(_has_terminalctrl GREATER -1)
            file(GLOB_RECURSE UPP_TERMINALCTRL_SOURCES CONFIGURE_DEPENDS "${UPP_ROOT}/Terminal/*.c" "${UPP_ROOT}/Terminal/*.cpp" "${UPP_ROOT}/Terminal/*.cxx" "${UPP_ROOT}/Terminal/*.cc" "${UPP_ROOT}/Terminal/*.icpp" "${UPP_ROOT}/Terminal/*.hpp")
            _upp_add_static_prefixed(_terminalctrl TerminalCtrl ${UPP_TERMINALCTRL_SOURCES})
            if(_out_suffix STREQUAL "")
                set_target_properties(${_terminalctrl} PROPERTIES OUTPUT_NAME "CoreTerminalCtrl" POSITION_INDEPENDENT_CODE ON)
            else()
                set_target_properties(${_terminalctrl} PROPERTIES OUTPUT_NAME "CoreTerminalCtrl-${_out_suffix}" POSITION_INDEPENDENT_CODE ON)
            endif()
            _upp_iface_link(${_terminalctrl})
        endif()
    endif()

    if(DEFINED UPP_OUT_INTERFACE)
        set(${UPP_OUT_INTERFACE} ${_iface} PARENT_SCOPE)
    endif()
endfunction()

# Backward compatible default build using global options (single global instance)
if(NOT TARGET upp-interface)
    set(_args)
    if(UPP_BUILD_PLUGIN_Z)
        list(APPEND _args PLUGIN_Z)
    endif()
    if(UPP_BUILD_CORE)
        list(APPEND _args CORE)
    endif()
    if(UPP_BUILD_PCRE)
      list(APPEND _args PCRE)
    endif()
    if(UPP_BUILD_SSL)
        list(APPEND _args SSL)
    endif()
    if(UPP_BUILD_SSH)
        list(APPEND _args SSH)
    endif()
    if(UPP_BUILD_ESC)
        list(APPEND _args ESC)
    endif()
    if(UPP_BUILD_SQL)
        list(APPEND _args SQL)
    endif()
    if(UPP_BUILD_MYSQL)
        list(APPEND _args MYSQL)
    endif()
    
    if(UPP_BUILD_DRAW)
        list(APPEND _args DRAW)
    endif()
    if(UPP_BUILD_BMP)
        list(APPEND _args BMP)
    endif()
    if(UPP_BUILD_JPEG)
        list(APPEND _args JPEG)
    endif()
    if(UPP_BUILD_PNG)
        list(APPEND _args PNG)
    endif()
    if(UPP_BUILD_PDFDRAW)
        list(APPEND _args PDFDRAW)
    endif()
    if(UPP_BUILD_RICHTEXT)
        list(APPEND _args RICHTEXT)
    endif()
    if(UPP_BUILD_RICHEDIT)
        list(APPEND _args RICHEDIT)
    endif()
    if(UPP_BUILD_PAINTER)
        list(APPEND _args PAINTER)
    endif()
    if(UPP_BUILD_CTRLCORE)
        list(APPEND _args CTRLCORE)
    endif()
    if(UPP_BUILD_CTRLLIB)
        list(APPEND _args CTRLLIB)
    endif()
    if(UPP_BUILD_GRIDLAY)
        list(APPEND _args GRIDLAY)
    endif()
    if(UPP_BUILD_GRIDCTRL)
        list(APPEND _args GRIDCTRL)
    endif()
    if(UPP_BUILD_TERMINALCTRL)
        list(APPEND _args TERMINALCTRL)
    endif()

    # Create legacy targets with fixed names by building with prefix = "" is not allowed.
    # Instead, create them with a known prefix and then alias to the legacy names.
    upp_build(_legacy ${_args} OUT_INTERFACE _legacy_iface)

    if(TARGET ${_legacy_iface} AND NOT TARGET upp-interface)
        add_library(upp-interface ALIAS ${_legacy_iface})
    endif()
    foreach(_pair IN ITEMS
        "plugin-z;upp-plugin-z;_legacy-upp-plugin-z"
        "core;upp-core;_legacy-upp-core"
        "pcre;upp-pcre;_legacy-upp-pcre"
        "ssl;upp-ssl;_legacy-upp-ssl"
        "ssh;upp-ssh;_legacy-upp-ssh"
        "esc;upp-esc;_legacy-upp-esc"
        "sql;upp-sql;_legacy-upp-sql"
        "mysql;upp-mysql;_legacy-upp-mysql"
        "draw;upp-draw;_legacy-upp-draw"
        "bmp;upp-bmp;_legacy-upp-bmp"
        "jpeg;upp-jpeg;_legacy-upp-jpeg"
        "png;upp-png;_legacy-upp-png"
        "pdfdraw;upp-pdfdraw;_legacy-upp-pdfdraw"
        "richtext;upp-richtext;_legacy-upp-richtext"
        "richedit;upp-richedit;_legacy-upp-richedit"
        "painter;upp-painter;_legacy-upp-painter"
        "CtrlCore;upp-CtrlCore;_legacy-upp-CtrlCore"
        "CtrlLib;upp-CtrlLib;_legacy-upp-CtrlLib"
        "GridLay;upp-GridLay;_legacy-upp-GridLay"
        "GridCtrl;upp-GridCtrl;_legacy-upp-GridCtrl"
        "TerminalCtrl;upp-TerminalCtrl;_legacy-upp-TerminalCtrl"
    )
        string(REPLACE ";" ";" _p "${_pair}")
        list(GET _p 1 _legacy_name)
        list(GET _p 2 _new_name)
        if(TARGET ${_new_name} AND NOT TARGET ${_legacy_name})
            add_library(${_legacy_name} ALIAS ${_new_name})
        endif()
    endforeach()
endif()

