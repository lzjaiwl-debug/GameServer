if(NOT TARGET mysql-interface)
    add_library(mysql-interface INTERFACE)
endif()

# 可配置 MySQL 根目录（可在命令行/GUI 覆盖）
set(MYSQL_ROOT "" CACHE PATH "Optional MySQL installation root")

# 优先使用用户提供的 MYSQL_ROOT，否则按平台常见路径搜索
if(WIN32)
    # 常见安装路径（可根据实际环境扩展）
    set(_mysql_default_roots
        "C:/Program Files/MySQL/MySQL Server 8.0"
        "C:/Program Files/MySQL/MySQL Server 5.7"
        "C:/MySQL"
    )
    if(NOT MYSQL_ROOT)
        foreach(_r IN LISTS _mysql_default_roots)
            if(EXISTS "${_r}")
                set(MYSQL_ROOT "${_r}")
                break()
            endif()
        endforeach()
    endif()

    # 计算 include 与 lib 目录（Win 下库通常在 lib 或 lib64）
    set(MYSQL_INCLUDE_DIR "${MYSQL_ROOT}/include")
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(MYSQL_LIB_DIR "${MYSQL_ROOT}/lib" CACHE PATH "" FORCE)
    else()
        set(MYSQL_LIB_DIR "${MYSQL_ROOT}/lib" CACHE PATH "" FORCE)
    endif()

    # 寻找库：优先 libmysql，其次 mysqlclient
    find_library(MYSQL_CLIENT
        NAMES libmysql mysqlclient
        PATHS "${MYSQL_LIB_DIR}"
        NO_DEFAULT_PATH
    )

    if(NOT MYSQL_CLIENT)
        message(FATAL_ERROR "MySQL client library not found. Set MYSQL_ROOT to your MySQL installation root (containing include/ and lib/).")
    endif()

    target_include_directories(mysql-interface INTERFACE "${MYSQL_INCLUDE_DIR}")
    target_link_directories(mysql-interface INTERFACE "${MYSQL_LIB_DIR}")
    target_link_libraries(mysql-interface INTERFACE ${MYSQL_CLIENT} ws2_32 crypt32)
else()
    # Linux: 尝试用系统路径或常见路径
    # 可用 apt/yum 安装的 dev 包，库名通常为 mysqlclient
    set(_mysql_common_include_paths
        "/usr/include/mysql"
        "/usr/local/include/mysql"
        "/opt/mysql/include"
    )
    set(_mysql_common_lib_paths
        "/usr/lib" "/usr/lib64"
        "/usr/local/lib" "/usr/local/lib64"
        "/opt/mysql/lib" "/opt/mysql/lib64"
    )

    if(MYSQL_ROOT)
        list(PREPEND _mysql_common_include_paths "${MYSQL_ROOT}/include")
        if(CMAKE_SIZEOF_VOID_P EQUAL 8)
            list(PREPEND _mysql_common_lib_paths "${MYSQL_ROOT}/lib64" "${MYSQL_ROOT}/lib")
        else()
            list(PREPEND _mysql_common_lib_paths "${MYSQL_ROOT}/lib")
        endif()
    endif()

    find_path(MYSQL_INCLUDE_DIR
        NAMES mysql.h
        PATH_SUFFIXES mysql
        PATHS ${_mysql_common_include_paths}
    )

    find_library(MYSQL_CLIENT
        NAMES mysqlclient
        PATHS ${_mysql_common_lib_paths}
    )

    if(NOT MYSQL_INCLUDE_DIR OR NOT MYSQL_CLIENT)
        message(FATAL_ERROR "MySQL headers or client library not found. Install MySQL/MariaDB development package (e.g. libmysqlclient-dev) or set MYSQL_ROOT.")
    endif()

    target_include_directories(mysql-interface INTERFACE "${MYSQL_INCLUDE_DIR}")
    target_link_libraries(mysql-interface INTERFACE ${MYSQL_CLIENT})
endif()