add_library(grandmp_windows_detection STATIC
    src/executable_detection.cpp
    src/hook_lifecycle.cpp
    src/module_residency.cpp
    src/player_position_diagnostics.cpp
)
target_compile_features(grandmp_windows_detection PUBLIC cxx_std_20)
target_include_directories(grandmp_windows_detection PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# Lifecycle prototype is kept separate from the production ASI target. It is
# consumed only by the disposable host and its tests in this slice.
add_library(grandmp_windows_lifecycle STATIC src/callback_lifecycle.cpp)
target_compile_features(grandmp_windows_lifecycle PUBLIC cxx_std_20)
target_include_directories(grandmp_windows_lifecycle PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ASI bootstrap state machine is platform-neutral; the experimental module
# wrapper below is deliberately Win32-only and remains separate from GrandMP.asi.
add_library(grandmp_asi_bootstrap STATIC src/asi_bootstrap.cpp)
target_compile_features(grandmp_asi_bootstrap PUBLIC cxx_std_20)
target_include_directories(grandmp_asi_bootstrap PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(grandmp_asi_bootstrap_experimental MODULE
        tools/asi_bootstrap_experimental.cpp
        tools/AsiBootstrapExperimental.def)
    target_compile_features(grandmp_asi_bootstrap_experimental PRIVATE cxx_std_20)
    target_link_libraries(grandmp_asi_bootstrap_experimental PRIVATE grandmp_asi_bootstrap)
    set_target_properties(grandmp_asi_bootstrap_experimental PROPERTIES
        PREFIX ""
        OUTPUT_NAME "GrandMPBootstrapExperimental"
        SUFFIX ".asi")

    add_executable(grandmp_asi_bootstrap_host tools/asi_bootstrap_host.cpp)
    add_dependencies(grandmp_asi_bootstrap_host grandmp_asi_bootstrap_experimental)

    add_library(grandmp_asi_loader_probe MODULE
        tools/asi_loader_probe_sentinel.cpp
        tools/AsiLoaderProbeSentinel.def)
    set_target_properties(grandmp_asi_loader_probe PROPERTIES
        PREFIX ""
        OUTPUT_NAME "GrandMPUalProbe"
        SUFFIX ".asi")
    add_executable(grandmp_asi_loader_probe_host tools/asi_loader_probe_host.cpp)
endif()

# Isolated Alpha 0.4 ped experiment. No dependency on any existing ASI or writer.
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(grandmp_ped_smoke MODULE tools/ped_smoke.cpp tools/ped_smoke.def)
    target_compile_features(grandmp_ped_smoke PRIVATE cxx_std_20)
    target_compile_options(grandmp_ped_smoke PRIVATE /JMC-)
    target_link_options(grandmp_ped_smoke PRIVATE /INCREMENTAL:NO)
    target_link_libraries(grandmp_ped_smoke PRIVATE grandmp_windows_detection bcrypt user32)
    set_target_properties(grandmp_ped_smoke PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPPedSmoke" SUFFIX ".asi")

    option(GRANDMP_BUILD_PED_SMOKE_ENABLED "Expose the separately reviewed physical ped smoke target" OFF)
    if(GRANDMP_BUILD_PED_SMOKE_ENABLED)
        add_library(grandmp_ped_smoke_enabled MODULE EXCLUDE_FROM_ALL tools/ped_smoke.cpp tools/ped_smoke.def)
        target_compile_features(grandmp_ped_smoke_enabled PRIVATE cxx_std_20)
        target_compile_definitions(grandmp_ped_smoke_enabled PRIVATE GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1)
        target_compile_options(grandmp_ped_smoke_enabled PRIVATE /JMC-)
        target_link_options(grandmp_ped_smoke_enabled PRIVATE /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:grandmp_ped_smoke_enabled>/GrandMPPedSmoke.map")
        target_link_libraries(grandmp_ped_smoke_enabled PRIVATE grandmp_windows_detection bcrypt user32)
        # Same admitted filename, different output directory: never overwrite the closed artifact.
        set_target_properties(grandmp_ped_smoke_enabled PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPPedSmoke" SUFFIX ".asi"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/ped-smoke-enabled"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/ped-smoke-enabled"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/ped-smoke-enabled")
    endif()
    option(GRANDMP_BUILD_PED_MOVE_ENABLED "Expose the separately reviewed physical ped movement target" OFF)
    if(GRANDMP_BUILD_PED_MOVE_ENABLED)
        # Defining this target does not build it. Explicit compilation/review is
        # separate from both the default artifact and the older F6/F8 target.
        add_library(grandmp_ped_smoke_move_enabled MODULE EXCLUDE_FROM_ALL tools/ped_smoke.cpp tools/ped_smoke.def)
        target_compile_features(grandmp_ped_smoke_move_enabled PRIVATE cxx_std_20)
        target_compile_definitions(grandmp_ped_smoke_move_enabled PRIVATE
            GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1 GRANDMP_PED_MOVE_PHYSICAL_BUILD=1)
        target_compile_options(grandmp_ped_smoke_move_enabled PRIVATE /JMC-)
        target_link_options(grandmp_ped_smoke_move_enabled PRIVATE /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:grandmp_ped_smoke_move_enabled>/GrandMPPedSmoke.map")
        target_link_libraries(grandmp_ped_smoke_move_enabled PRIVATE grandmp_windows_detection bcrypt user32)
        set_target_properties(grandmp_ped_smoke_move_enabled PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPPedSmoke" SUFFIX ".asi"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/ped-smoke-move-enabled"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/ped-smoke-move-enabled"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/ped-smoke-move-enabled")
    endif()
endif()

if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Alpha 0.6 default artifact stays physically closed.
    add_library(grandmp_remote_ped_experimental MODULE
        tools/ped_smoke.cpp tools/remote_ped.def src/observer_network.cpp)
    target_compile_features(grandmp_remote_ped_experimental PRIVATE cxx_std_20)
    target_compile_definitions(grandmp_remote_ped_experimental PRIVATE GRANDMP_REMOTE_PED_VARIANT=1)
    target_compile_options(grandmp_remote_ped_experimental PRIVATE /JMC-)
    target_link_options(grandmp_remote_ped_experimental PRIVATE /INCREMENTAL:NO)
    target_link_libraries(grandmp_remote_ped_experimental PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
    set_target_properties(grandmp_remote_ped_experimental PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPRemotePedExperimental" SUFFIX ".asi"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped")

    option(GRANDMP_BUILD_REMOTE_PED_ENABLED "Expose the separately reviewed physical remote ped target" OFF)
    if(GRANDMP_BUILD_REMOTE_PED_ENABLED)
        add_library(grandmp_remote_ped_enabled MODULE EXCLUDE_FROM_ALL
            tools/ped_smoke.cpp tools/remote_ped.def src/observer_network.cpp)
        target_compile_features(grandmp_remote_ped_enabled PRIVATE cxx_std_20)
        target_compile_definitions(grandmp_remote_ped_enabled PRIVATE GRANDMP_REMOTE_PED_VARIANT=1
            GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1 GRANDMP_REMOTE_PED_PHYSICAL_BUILD=1)
        target_compile_options(grandmp_remote_ped_enabled PRIVATE /JMC-)
        target_link_options(grandmp_remote_ped_enabled PRIVATE /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:grandmp_remote_ped_enabled>/GrandMPRemotePedExperimental.map")
        target_link_libraries(grandmp_remote_ped_enabled PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
        set_target_properties(grandmp_remote_ped_enabled PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPRemotePedExperimental" SUFFIX ".asi"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-enabled"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-enabled"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-enabled")
    endif()
endif()

if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_library(grandmp_remote_ped_move_experimental MODULE EXCLUDE_FROM_ALL
        tools/ped_smoke.cpp tools/remote_ped_move.def src/observer_network.cpp)
    target_compile_features(grandmp_remote_ped_move_experimental PRIVATE cxx_std_20)
    target_compile_definitions(grandmp_remote_ped_move_experimental PRIVATE GRANDMP_REMOTE_PED_VARIANT=1 GRANDMP_REMOTE_PED_MOVE_VARIANT=1)
    target_compile_options(grandmp_remote_ped_move_experimental PRIVATE /JMC-)
    target_link_options(grandmp_remote_ped_move_experimental PRIVATE /INCREMENTAL:NO)
    target_link_libraries(grandmp_remote_ped_move_experimental PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
    set_target_properties(grandmp_remote_ped_move_experimental PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPRemotePedMoveExperimental" SUFFIX ".asi"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-move"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-move"
        ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-move")
    option(GRANDMP_BUILD_REMOTE_PED_MOVE_ENABLED "Expose the explicitly approved remote movement target" OFF)
    if(GRANDMP_BUILD_REMOTE_PED_MOVE_ENABLED)
        add_library(grandmp_remote_ped_move_enabled MODULE EXCLUDE_FROM_ALL
            tools/ped_smoke.cpp tools/remote_ped_move.def src/observer_network.cpp)
        target_compile_features(grandmp_remote_ped_move_enabled PRIVATE cxx_std_20)
        target_compile_definitions(grandmp_remote_ped_move_enabled PRIVATE GRANDMP_REMOTE_PED_VARIANT=1 GRANDMP_REMOTE_PED_MOVE_VARIANT=1
            GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1 GRANDMP_REMOTE_PED_PHYSICAL_BUILD=1 GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD=1)
        target_compile_options(grandmp_remote_ped_move_enabled PRIVATE /JMC-)
        target_link_options(grandmp_remote_ped_move_enabled PRIVATE /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:grandmp_remote_ped_move_enabled>/GrandMPRemotePedMoveExperimental.map")
        target_link_libraries(grandmp_remote_ped_move_enabled PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
        set_target_properties(grandmp_remote_ped_move_enabled PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPRemotePedMoveExperimental" SUFFIX ".asi"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-move-enabled"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-move-enabled"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-move-enabled")
    endif()
endif()

if(WIN32)
    add_library(grandmp_callback_lifetime_module MODULE
        tools/callback_dll_lifetime_module.cpp
        tools/CallbackLifetimeLab.def)
    target_compile_features(grandmp_callback_lifetime_module PRIVATE cxx_std_20)
    target_compile_definitions(grandmp_callback_lifetime_module PRIVATE GMP_CALLBACK_LIFETIME_MODULE)
    target_link_libraries(grandmp_callback_lifetime_module PRIVATE grandmp_windows_lifecycle)
    set_target_properties(grandmp_callback_lifetime_module PROPERTIES
        PREFIX ""
        OUTPUT_NAME "grandmp_callback_lifetime_module"
        SUFFIX ".dll")

    add_executable(grandmp_callback_dll_lifetime_host tools/callback_dll_lifetime_host.cpp)
    target_compile_features(grandmp_callback_dll_lifetime_host PRIVATE cxx_std_20)
    add_dependencies(grandmp_callback_dll_lifetime_host grandmp_callback_lifetime_module)
endif()

add_library(grandmp_position_observer_reader STATIC src/position_observer.cpp)
target_compile_features(grandmp_position_observer_reader PUBLIC cxx_std_20)
target_include_directories(grandmp_position_observer_reader PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(grandmp_callback_lifecycle_host tools/callback_lifecycle_host.cpp)
    target_compile_features(grandmp_callback_lifecycle_host PRIVATE cxx_std_20)
    target_link_libraries(grandmp_callback_lifecycle_host PRIVATE grandmp_windows_lifecycle)

    add_executable(grandmp_position_observer
        tools/position_observer.cpp
        src/observer_network.cpp
    )
    target_compile_features(grandmp_position_observer PRIVATE cxx_std_20)
    target_link_libraries(grandmp_position_observer PRIVATE
        grandmp_position_observer_reader grandmp_windows_detection
        GrandMP::Shared bcrypt ws2_32)

    add_executable(grandmp_remote_scene_host
        tools/remote_scene_preview_host.cpp
        src/observer_network.cpp)
    target_compile_features(grandmp_remote_scene_host PRIVATE cxx_std_20)
    target_include_directories(grandmp_remote_scene_host PRIVATE include)
    target_link_libraries(grandmp_remote_scene_host PRIVATE GrandMP::Shared ws2_32 bcrypt)

    add_executable(grandmp_window_observer tools/window_observer.cpp)
    target_compile_features(grandmp_window_observer PRIVATE cxx_std_20)
    target_link_libraries(grandmp_window_observer PRIVATE user32)

    add_library(grandmp_windows MODULE src/asi_entry.cpp src/GrandMP.def)
    target_compile_features(grandmp_windows PRIVATE cxx_std_20)
    if(MSVC)
        target_compile_options(grandmp_windows PRIVATE /EHsc)
    endif()
    target_link_libraries(grandmp_windows PRIVATE GrandMP::Shared grandmp_windows_detection bcrypt)
    set_target_properties(grandmp_windows PROPERTIES
        PREFIX ""
        OUTPUT_NAME "GrandMP"
        SUFFIX ".asi"
    )
endif()

# Counter prototype with a shared wrapper and READY descriptor. The experimental
# ASI admits explicit preparation in the cooperative host; its existing worker
# can also opt into read-only Hoodlum binding before READY. No engine dispatch,
# callback registration or GTA writer is introduced.
if(WIN32)
    add_library(grandmp_native_counter_lab MODULE
        tools/native_counter_experimental.cpp tools/native_counter_lab.def)
    target_compile_features(grandmp_native_counter_lab PRIVATE cxx_std_20)
    target_compile_definitions(grandmp_native_counter_lab PRIVATE GMP_NATIVE_COUNTER_HOST_TEST)
    target_link_libraries(grandmp_native_counter_lab PRIVATE grandmp_asi_bootstrap)
    set_target_properties(grandmp_native_counter_lab PROPERTIES PREFIX ""
        OUTPUT_NAME "GrandMPNativeCounterLab" SUFFIX ".dll")

    add_executable(grandmp_native_counter_host tools/native_counter_host.cpp)
    target_compile_features(grandmp_native_counter_host PRIVATE cxx_std_20)
    target_link_libraries(grandmp_native_counter_host PRIVATE grandmp_windows_detection)
    add_dependencies(grandmp_native_counter_host grandmp_native_counter_lab)

    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        add_library(grandmp_native_counter_experimental MODULE
            tools/native_counter_experimental.cpp tools/native_counter_experimental.def)
        target_compile_features(grandmp_native_counter_experimental PRIVATE cxx_std_20)
        target_link_libraries(grandmp_native_counter_experimental PRIVATE grandmp_asi_bootstrap)
        if(MSVC)
            target_compile_options(grandmp_native_counter_experimental PRIVATE /JMC-)
            target_link_options(grandmp_native_counter_experimental PRIVATE /INCREMENTAL:NO)
        endif()
        set_target_properties(grandmp_native_counter_experimental PROPERTIES PREFIX ""
            OUTPUT_NAME "GrandMPNativeCounterExperimental" SUFFIX ".asi")
        add_dependencies(grandmp_native_counter_host grandmp_native_counter_experimental)
    endif()
endif()

# Read-only physical evidence preflight. This target has no GTA writer.
add_library(grandmp_native_counter_preflight STATIC tools/native_counter_preflight.cpp)
target_compile_features(grandmp_native_counter_preflight PUBLIC cxx_std_20)
target_include_directories(grandmp_native_counter_preflight PUBLIC tools)
target_link_libraries(grandmp_native_counter_preflight PUBLIC grandmp_windows_detection bcrypt)

if(TARGET grandmp_native_counter_lab)
    target_link_libraries(grandmp_native_counter_lab PRIVATE grandmp_native_counter_preflight)
endif()
if(TARGET grandmp_native_counter_experimental)
    target_link_libraries(grandmp_native_counter_experimental PRIVATE grandmp_native_counter_preflight)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(grandmp_native_counter_diagnostic tools/native_counter_diagnostic.cpp)
    target_link_libraries(grandmp_native_counter_diagnostic PRIVATE grandmp_native_counter_preflight)
endif()

# External transaction is confined to the existing disposable counter host.
# No GTA writer is linked into the experimental ASI target.
if(WIN32 AND MSVC)
    target_compile_options(grandmp_native_counter_host PRIVATE /JMC-)
    target_compile_options(grandmp_native_counter_lab PRIVATE /JMC-)
    target_link_options(grandmp_native_counter_host PRIVATE /INCREMENTAL:NO)
    target_link_options(grandmp_native_counter_lab PRIVATE /INCREMENTAL:NO)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        target_sources(grandmp_native_counter_host PRIVATE tools/native_counter_host.def)
        target_link_options(grandmp_native_counter_host PRIVATE
            "/DEF:${CMAKE_CURRENT_SOURCE_DIR}/tools/native_counter_host.def")
        add_executable(grandmp_native_counter_transaction tools/native_counter_transaction.cpp)
        target_compile_features(grandmp_native_counter_transaction PRIVATE cxx_std_20)
        target_link_libraries(grandmp_native_counter_transaction PRIVATE grandmp_native_counter_preflight)
        add_dependencies(grandmp_native_counter_transaction grandmp_native_counter_host grandmp_native_counter_lab)
    endif()
endif()
