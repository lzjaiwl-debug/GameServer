if(NOT TARGET openssl-interface)
    add_library(openssl-interface INTERFACE)
endif()

# 统一 OpenSSL 依赖入口：
# 在需要的目标里：
#   include(${CMAKE_SOURCE_DIR}/cmake/openssl.cmake)
#   target_link_libraries(your_target PRIVATE openssl-interface)

if(WIN32)
    # 优先使用仓库内置 OpenSSL（静态库）
    # 默认布局：third-party/openssl/include/openssl/*.h
    set(OPENSSL_ROOT_DIR "${CMAKE_SOURCE_DIR}/third-party/openssl" CACHE PATH "Bundled OpenSSL root")
    set(OPENSSL_INCLUDE_DIR "${OPENSSL_ROOT_DIR}/include" CACHE PATH "Bundled OpenSSL include dir")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(OPENSSL_LIB_DIR "${OPENSSL_ROOT_DIR}/lib64" CACHE PATH "Bundled OpenSSL library dir")
    else()
        set(OPENSSL_LIB_DIR "${OPENSSL_ROOT_DIR}/lib32" CACHE PATH "Bundled OpenSSL library dir")
    endif()

    target_include_directories(openssl-interface INTERFACE ${OPENSSL_INCLUDE_DIR})

    # Some OpenSSL bundles place .lib files under an extra `lib/` directory.
    if(IS_DIRECTORY "${OPENSSL_LIB_DIR}/lib")
        set(_openssl_effective_lib_dir "${OPENSSL_LIB_DIR}/lib")
    else()
        set(_openssl_effective_lib_dir "${OPENSSL_LIB_DIR}")
    endif()
    target_link_directories(openssl-interface INTERFACE ${_openssl_effective_lib_dir})

    # Detect actual library file names.
    set(_ssl_lib "")
    set(_crypto_lib "")
    if(EXISTS "${_openssl_effective_lib_dir}/libssl.lib" AND EXISTS "${_openssl_effective_lib_dir}/libcrypto.lib")
        set(_ssl_lib "libssl.lib")
        set(_crypto_lib "libcrypto.lib")
    elseif(EXISTS "${_openssl_effective_lib_dir}/ssl.lib" AND EXISTS "${_openssl_effective_lib_dir}/crypto.lib")
        set(_ssl_lib "ssl.lib")
        set(_crypto_lib "crypto.lib")
    else()
        message(FATAL_ERROR "OpenSSL libs not found in ${_openssl_effective_lib_dir}. Expected libssl.lib/libcrypto.lib or ssl.lib/crypto.lib")
    endif()

    # Link via full paths to avoid MSVC search/path quirks and eliminate LNK1104.
    target_link_libraries(openssl-interface INTERFACE
        "${_openssl_effective_lib_dir}/${_ssl_lib}"
        "${_openssl_effective_lib_dir}/${_crypto_lib}"
        advapi32 comdlg32 comctl32 imm32 winmm
    )

elseif(UNIX)
    # Linux/Unix：使用系统 OpenSSL（或由 toolchain/环境提供的 OpenSSL）
    find_package(OpenSSL REQUIRED)
    find_package(Threads REQUIRED)

    # OpenSSL::SSL / OpenSSL::Crypto 通常会自行携带 include dirs
    target_link_libraries(openssl-interface INTERFACE OpenSSL::SSL OpenSSL::Crypto Threads::Threads)
endif()
