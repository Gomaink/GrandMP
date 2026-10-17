cmake_minimum_required(VERSION 3.20)
project(GrandMPMinHookIsolated LANGUAGES C CXX)

if(NOT WIN32 OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "This disposable hook probe requires Windows x86")
endif()
if(NOT DEFINED MINHOOK_SOURCE_DIR)
    message(FATAL_ERROR "Pass -DMINHOOK_SOURCE_DIR=<MinHook v1.3.4 checkout>")
endif()
find_package(Git REQUIRED)
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MINHOOK_SOURCE_DIR}" rev-parse HEAD
                OUTPUT_VARIABLE minhook_revision OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_result)
if(NOT git_result EQUAL 0 OR
   NOT minhook_revision STREQUAL "c3fcafdc10146beb5919319d0683e44e3c30d537")
    message(FATAL_ERROR "MinHook source must be v1.3.4 at c3fcafdc10146beb5919319d0683e44e3c30d537")
endif()
execute_process(COMMAND "${GIT_EXECUTABLE}" -C "${MINHOOK_SOURCE_DIR}" status --porcelain
                OUTPUT_VARIABLE minhook_changes OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE git_result)
if(NOT git_result EQUAL 0 OR NOT minhook_changes STREQUAL "")
    message(FATAL_ERROR "MinHook source checkout must be clean")
endif()

include(CTest)
add_subdirectory("${MINHOOK_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/minhook")

add_library(probe_target SHARED probe_target.cpp)
target_compile_features(probe_target PRIVATE cxx_std_20)
add_library(probe_detour SHARED probe_detour.cpp)
target_compile_features(probe_detour PRIVATE cxx_std_20)
add_executable(minhook_isolated_tests minhook_isolated_tests.cpp)
target_compile_features(minhook_isolated_tests PRIVATE cxx_std_20)
target_link_libraries(minhook_isolated_tests PRIVATE minhook)
add_dependencies(minhook_isolated_tests probe_target probe_detour)
add_test(NAME minhook_own_function
         COMMAND minhook_isolated_tests $<TARGET_FILE:probe_target> $<TARGET_FILE:probe_detour>)
