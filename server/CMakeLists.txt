add_library(grandmp_server_sessions STATIC src/sessions.cpp)
target_compile_features(grandmp_server_sessions PUBLIC cxx_std_20)
target_include_directories(grandmp_server_sessions PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_link_libraries(grandmp_server_sessions PUBLIC GrandMP::Shared)

add_executable(grandmp_server src/main.cpp)
target_compile_features(grandmp_server PRIVATE cxx_std_20)
target_link_libraries(grandmp_server PRIVATE grandmp_server_sessions)
if(WIN32)
    target_link_libraries(grandmp_server PRIVATE ws2_32)
endif()
