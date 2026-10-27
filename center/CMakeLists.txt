add_executable(CenterServer main.cpp)
target_link_libraries(CenterServer PRIVATE gs_common)

if(COMMAND base_interface_export_all_headers)
    base_interface_export_all_headers(CenterServer)
endif()
