cmake_minimum_required(VERSION 3.20)

project(GrandMP VERSION 0.2.0 LANGUAGES CXX)

include(CTest)

add_subdirectory(shared)
add_subdirectory(server)
add_subdirectory(client/synthetic)
if(WIN32)
    add_subdirectory(client/windows)
endif()
add_subdirectory(tests)
