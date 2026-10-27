add_executable(TestNode main.cpp)
target_link_libraries(TestNode PRIVATE gs_common)

if(COMMAND base_interface_export_all_headers)
    base_interface_export_all_headers(TestNode)
endif()
