add_library(grandmp_shared STATIC
    src/logger.cpp
    src/protocol.cpp
    src/remote_roster.cpp
    src/remote_player_scene.cpp
    src/remote_player_adapter.cpp
    src/version.cpp
)

target_compile_features(grandmp_shared PUBLIC cxx_std_20)
if(MSVC)
    target_compile_options(grandmp_shared PRIVATE /EHsc)
endif()
target_compile_definitions(grandmp_shared PRIVATE
    GRANDMP_VERSION_MAJOR=${PROJECT_VERSION_MAJOR}
    GRANDMP_VERSION_MINOR=${PROJECT_VERSION_MINOR}
    GRANDMP_VERSION_PATCH=${PROJECT_VERSION_PATCH}
)
target_include_directories(grandmp_shared PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

add_library(GrandMP::Shared ALIAS grandmp_shared)
