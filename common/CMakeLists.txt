file(GLOB GS_COMMON_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.cpp")
file(GLOB GS_COMMON_HEADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.h")

add_library(gs_common STATIC ${GS_COMMON_SOURCES} ${GS_COMMON_HEADERS})

target_include_directories(gs_common PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(gs_common PUBLIC cxx_std_17)
target_link_libraries(gs_common PUBLIC base-interface)

if(WIN32)
    target_link_libraries(gs_common PUBLIC ws2_32)
endif()

source_group(TREE "${CMAKE_CURRENT_SOURCE_DIR}" PREFIX "common" FILES ${GS_COMMON_SOURCES} ${GS_COMMON_HEADERS})
