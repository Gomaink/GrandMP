add_library(grandmp_windows_detection STATIC
    src/executable_detection.cpp
    src/hook_lifecycle.cpp
    src/module_residency.cpp
    src/player_position_diagnostics.cpp
)

option(GRANDMP_BUILD_CLEO_PLAYER_BOOTSTRAP "Expose isolated CLEO4 slot-1 factory prototype" OFF)
option(GRANDMP_ENABLE_CLEO_PLAYER_SYNC "Add real-client capture and existing remote replay to CLEO prototype" OFF)
option(GRANDMP_BUILD_REMOTE_PLAYER_FOUNDATION "Expose isolated Alpha 0.10 pool-owned CPlayerPed foundation" OFF)
option(GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF "Expose isolated Alpha 0.11 single-GTA native input proof (no UDP/presentation)" OFF)
option(GRANDMP_ENABLE_AUTHENTICATED_PAD_COW "Enable exact Hoodlum pad1 data exception in the independent proof only" OFF)
option(GRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC "Add bounded read-only Alpha 0.11.1-D1 tail diagnostics to the independent proof" OFF)
option(GRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT "Enable the exact Alpha 0.11.2 LastTimeTouched contract in the isolated proof" OFF)
if(GRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT AND NOT GRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC)
    message(FATAL_ERROR "Semantic pad context requires the isolated D1 diagnostics, independent proof and authenticated pad COW")
endif()
if(GRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC AND NOT (GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF AND GRANDMP_ENABLE_AUTHENTICATED_PAD_COW))
    message(FATAL_ERROR "Remote tail D1 requires the isolated independent proof and its authenticated pad COW contract")
endif()
if(GRANDMP_ENABLE_AUTHENTICATED_PAD_COW AND NOT GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF)
    message(FATAL_ERROR "Authenticated pad COW requires the isolated independent proof")
endif()
option(GRANDMP_ENABLE_MINIMAL_NATIVE_LIFECYCLE "Expose Alpha 0.13 minimal removal in the isolated Alpha 0.11.2 proof" OFF)
if(GRANDMP_ENABLE_MINIMAL_NATIVE_LIFECYCLE AND NOT (GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF AND
    GRANDMP_ENABLE_AUTHENTICATED_PAD_COW AND GRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC AND GRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT))
    message(FATAL_ERROR "Minimal lifecycle requires the complete validated Alpha 0.11.2 proof profile")
endif()
option(GRANDMP_ENABLE_INDEPENDENT_PLAYER_UDP "Receive UDP input in the isolated independent CPlayerPed profile; no presentation/removal" OFF)
if(GRANDMP_ENABLE_INDEPENDENT_PLAYER_UDP AND NOT GRANDMP_ENABLE_MINIMAL_NATIVE_LIFECYCLE)
    message(FATAL_ERROR "Independent UDP requires the complete validated Alpha 0.13.1 profile")
endif()
option(GRANDMP_ENABLE_REAL_PLAYER_SENDER "Capture local CJ into existing UDP snapshots; sender-only, no native remote" OFF)
if(GRANDMP_ENABLE_REAL_PLAYER_SENDER AND NOT GRANDMP_ENABLE_INDEPENDENT_PLAYER_UDP)
    message(FATAL_ERROR "Real sender requires the isolated independent UDP profile")
endif()
set(GRANDMP_CLEO4_SDK_DIR "" CACHE PATH "Exact CLEO4 4.4.4 cleo_sdk directory")
set(GRANDMP_SANNY_COMPILER "" CACHE FILEPATH "Sanny Builder compiler, used offline only")
if((GRANDMP_BUILD_CLEO_PLAYER_BOOTSTRAP AND GRANDMP_BUILD_REMOTE_PLAYER_FOUNDATION) OR
   (GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF AND (GRANDMP_BUILD_CLEO_PLAYER_BOOTSTRAP OR GRANDMP_BUILD_REMOTE_PLAYER_FOUNDATION)))
    message(FATAL_ERROR "Select one CLEO player variant; never coinstall them")
endif()
if(GRANDMP_BUILD_CLEO_PLAYER_BOOTSTRAP OR GRANDMP_BUILD_REMOTE_PLAYER_FOUNDATION OR GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF)
    if(NOT WIN32 OR NOT MSVC OR NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
        message(FATAL_ERROR "CLEO bootstrap requires MSVC Win32")
    endif()
    if(NOT EXISTS "${GRANDMP_CLEO4_SDK_DIR}/CLEO.lib" OR NOT EXISTS "${GRANDMP_CLEO4_SDK_DIR}/CLEO.h" OR NOT EXISTS "${GRANDMP_SANNY_COMPILER}")
        message(FATAL_ERROR "Explicit CLEO4 SDK and Sanny compiler required")
    endif()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(cleo_generated "${CMAKE_CURRENT_BINARY_DIR}/cleo-player-script")
    add_custom_command(OUTPUT "${cleo_generated}/script_image.hpp" "${cleo_generated}/GrandMPPlayerBootstrap.cs"
        COMMAND "${Python3_EXECUTABLE}" "${PROJECT_SOURCE_DIR}/scripts/build_cleo_script.py"
            --compiler "${GRANDMP_SANNY_COMPILER}" --source "${CMAKE_CURRENT_SOURCE_DIR}/cleo/GrandMPPlayerBootstrap.txt"
            --output "${cleo_generated}/GrandMPPlayerBootstrap.cs" --header "${cleo_generated}/script_image.hpp"
        DEPENDS cleo/GrandMPPlayerBootstrap.txt "${PROJECT_SOURCE_DIR}/scripts/build_cleo_script.py" VERBATIM)
    if(GRANDMP_BUILD_INDEPENDENT_NATIVE_CONTROL_PROOF)
        add_library(grandmp_independent_native_control_proof MODULE EXCLUDE_FROM_ALL cleo/player_bootstrap.cpp
            "${cleo_generated}/script_image.hpp" src/pooled_remote_player.cpp src/remote_player_binding.cpp)
        target_compile_features(grandmp_independent_native_control_proof PRIVATE cxx_std_20)
        target_compile_options(grandmp_independent_native_control_proof PRIVATE /W4 /WX /EHsc /Oy- /JMC-)
        target_link_options(grandmp_independent_native_control_proof PRIVATE /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:grandmp_independent_native_control_proof>/GrandMPPlayerBootstrap.map")
        target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_POOLED_REMOTE_PLAYER_BUILD=1
            GRANDMP_CLEO_PLAYER_SYNC_BUILD=1 GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD=1
            GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=0 GRANDMP_NATIVE_INPUT_REPLAY_BUILD=0)
        if(GRANDMP_ENABLE_REAL_PLAYER_SENDER)
            target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_REAL_PLAYER_SENDER_BUILD=1 GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD=1)
        else()
            target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD=0)
        endif()
        if(GRANDMP_ENABLE_AUTHENTICATED_PAD_COW)
            target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_AUTHENTICATED_PAD_COW_BUILD=1)
        endif()
        if(GRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC)
            target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_REMOTE_TAIL_DIAGNOSTIC_BUILD=1)
        endif()
        if(GRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT)
            target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_SEMANTIC_PAD_CONTEXT_BUILD=1)
        endif()
        target_include_directories(grandmp_independent_native_control_proof PRIVATE include "${GRANDMP_CLEO4_SDK_DIR}" "${cleo_generated}")
        target_link_libraries(grandmp_independent_native_control_proof PRIVATE GrandMP::Shared "${GRANDMP_CLEO4_SDK_DIR}/CLEO.lib" bcrypt user32)
        set_target_properties(grandmp_independent_native_control_proof PROPERTIES PREFIX "" SUFFIX ".cleo" OUTPUT_NAME "GrandMPPlayerBootstrap"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof")
        if(GRANDMP_ENABLE_AUTHENTICATED_PAD_COW)
            set_target_properties(grandmp_independent_native_control_proof PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof-pad-cow"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof-pad-cow")
        endif()
        if(GRANDMP_ENABLE_REMOTE_TAIL_DIAGNOSTIC)
            set_target_properties(grandmp_independent_native_control_proof PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof-pad-cow-tail-d1"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof-pad-cow-tail-d1")
        endif()
        if(GRANDMP_ENABLE_SEMANTIC_PAD_CONTEXT)
            set_target_properties(grandmp_independent_native_control_proof PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof-semantic-pad"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-native-control-proof-semantic-pad")
        endif()
        if(GRANDMP_ENABLE_MINIMAL_NATIVE_LIFECYCLE)
            target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD=1)
            set_target_properties(grandmp_independent_native_control_proof PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/minimal-native-lifecycle"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/minimal-native-lifecycle")
        endif()
        if(GRANDMP_ENABLE_INDEPENDENT_PLAYER_UDP)
            target_compile_definitions(grandmp_independent_native_control_proof PRIVATE GRANDMP_INDEPENDENT_PLAYER_UDP_BUILD=1)
            target_sources(grandmp_independent_native_control_proof PRIVATE src/observer_network.cpp)
            target_link_libraries(grandmp_independent_native_control_proof PRIVATE ws2_32)
            set_target_properties(grandmp_independent_native_control_proof PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-player-udp"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-player-udp")
        endif()
        if(GRANDMP_ENABLE_REAL_PLAYER_SENDER)
            set_target_properties(grandmp_independent_native_control_proof PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-player-sender"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/independent-player-sender")
        endif()
    elseif(GRANDMP_BUILD_REMOTE_PLAYER_FOUNDATION)
        add_library(grandmp_remote_player_foundation MODULE EXCLUDE_FROM_ALL cleo/player_bootstrap.cpp
            "${cleo_generated}/script_image.hpp" src/pooled_remote_player.cpp src/remote_player_binding.cpp src/observer_network.cpp)
        target_compile_features(grandmp_remote_player_foundation PRIVATE cxx_std_20)
        target_compile_options(grandmp_remote_player_foundation PRIVATE /W4 /WX /EHsc /Oy- /JMC-)
        target_link_options(grandmp_remote_player_foundation PRIVATE /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:grandmp_remote_player_foundation>/GrandMPPlayerBootstrap.map")
        target_compile_definitions(grandmp_remote_player_foundation PRIVATE GRANDMP_POOLED_REMOTE_PLAYER_BUILD=1
            GRANDMP_CLEO_PLAYER_SYNC_BUILD=1 GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD=1
            GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=0 GRANDMP_NATIVE_INPUT_REPLAY_BUILD=0)
        target_include_directories(grandmp_remote_player_foundation PRIVATE include "${GRANDMP_CLEO4_SDK_DIR}" "${cleo_generated}")
        target_link_libraries(grandmp_remote_player_foundation PRIVATE GrandMP::Shared "${GRANDMP_CLEO4_SDK_DIR}/CLEO.lib" bcrypt user32 ws2_32)
        set_target_properties(grandmp_remote_player_foundation PROPERTIES PREFIX "" SUFFIX ".cleo" OUTPUT_NAME "GrandMPPlayerBootstrap"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/remote-player-foundation"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/remote-player-foundation")
    else()
    add_library(grandmp_cleo_player_bootstrap MODULE EXCLUDE_FROM_ALL cleo/player_bootstrap.cpp
        "${cleo_generated}/script_image.hpp" src/native_second_player.cpp src/remote_player_binding.cpp src/observer_network.cpp)
    target_compile_features(grandmp_cleo_player_bootstrap PRIVATE cxx_std_20)
    target_compile_options(grandmp_cleo_player_bootstrap PRIVATE /W4 /WX /EHsc /Oy- /JMC-)
    target_compile_definitions(grandmp_cleo_player_bootstrap PRIVATE GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=1)
    if(GRANDMP_ENABLE_CLEO_PLAYER_SYNC)
        target_compile_definitions(grandmp_cleo_player_bootstrap PRIVATE
            GRANDMP_CLEO_PLAYER_SYNC_BUILD=1 GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD=1
            GRANDMP_NATIVE_INPUT_REPLAY_BUILD=1 GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1)
    endif()
    target_include_directories(grandmp_cleo_player_bootstrap PRIVATE include "${GRANDMP_CLEO4_SDK_DIR}" "${cleo_generated}")
    target_link_libraries(grandmp_cleo_player_bootstrap PRIVATE GrandMP::Shared "${GRANDMP_CLEO4_SDK_DIR}/CLEO.lib" bcrypt user32 ws2_32)
    set_target_properties(grandmp_cleo_player_bootstrap PROPERTIES PREFIX "" SUFFIX ".cleo" OUTPUT_NAME "GrandMPPlayerBootstrap"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cleo-player-bootstrap"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cleo-player-bootstrap")
    if(GRANDMP_ENABLE_CLEO_PLAYER_SYNC)
        set_target_properties(grandmp_cleo_player_bootstrap PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cleo-player-sync"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/cleo-player-sync")
    endif()
    endif()
endif()

# Standalone, strictly read-only Win32/x86 external capture tool for one
# supervised Hoodlum memory snapshot. This is not an ASI and is never part of
# the default build or loaded into the game.
option(GRANDMP_BUILD_ALPHA09_HOODLUM_MEMORY_READER
    "Expose isolated read-only external Hoodlum memory reader (Win32/x86)" OFF)
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND GRANDMP_BUILD_ALPHA09_HOODLUM_MEMORY_READER)
    add_executable(grandmp_alpha09_hoodlum_memory_reader EXCLUDE_FROM_ALL
        tools/alpha09_hoodlum_memory_reader.cpp)
    target_compile_features(grandmp_alpha09_hoodlum_memory_reader PRIVATE cxx_std_20)
    target_compile_options(grandmp_alpha09_hoodlum_memory_reader PRIVATE /W4 /WX /EHsc /JMC-)
    target_link_options(grandmp_alpha09_hoodlum_memory_reader PRIVATE /INCREMENTAL:NO)
    target_link_libraries(grandmp_alpha09_hoodlum_memory_reader PRIVATE bcrypt psapi)
    set_target_properties(grandmp_alpha09_hoodlum_memory_reader PROPERTIES
        MSVC_RUNTIME_LIBRARY "MultiThreadedDebug"
        OUTPUT_NAME "GrandMPAlpha09HoodlumMemoryReader"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/alpha09-hoodlum-memory-reader/$<CONFIG>")
endif()

# Own remote CPlayerPed registry/context scope, CLOSED static library only.
# No ASI links this target; exposing it does not authenticate a native backend.
option(GRANDMP_BUILD_NATIVE_REMOTE_PLAYER_BINDING "Expose the closed Win32 CPlayerPed binding library (no ASI)" OFF)
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND GRANDMP_BUILD_NATIVE_REMOTE_PLAYER_BINDING)
    add_library(grandmp_native_remote_player_binding STATIC EXCLUDE_FROM_ALL
        src/remote_player_binding.cpp src/remote_player_hoodlum.cpp)
    target_compile_features(grandmp_native_remote_player_binding PUBLIC cxx_std_20)
    target_compile_options(grandmp_native_remote_player_binding PRIVATE /W4 /WX /EHsc)
    target_include_directories(grandmp_native_remote_player_binding PUBLIC include)
    target_link_libraries(grandmp_native_remote_player_binding PUBLIC GrandMP::Shared grandmp_windows_detection bcrypt)
    # Intentionally NO GRANDMP_NATIVE_REMOTE_PLAYER_BINDING_BUILD=1. Even this
    # opt-in library keeps the default Binding<> closed. Tests use explicit fakes.
endif()

# Own ProcessControl slot wrapper; factory/lifecycle stay in the existing host.
# Exposed separately so the homologated factory target never gains input replay.
option(GRANDMP_BUILD_NATIVE_PLAYER_INPUT_EXPERIMENTAL "Expose isolated Hoodlum on-foot input ASI" OFF)
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND GRANDMP_BUILD_NATIVE_PLAYER_INPUT_EXPERIMENTAL)
    add_library(grandmp_native_player_input_experimental MODULE EXCLUDE_FROM_ALL
        tools/ped_smoke.cpp tools/NativePlayerExperimental.def
        src/native_second_player.cpp src/remote_player_binding.cpp src/observer_network.cpp)
    target_compile_features(grandmp_native_player_input_experimental PRIVATE cxx_std_20)
    target_include_directories(grandmp_native_player_input_experimental PRIVATE include)
    target_compile_options(grandmp_native_player_input_experimental PRIVATE /JMC- /W4 /WX /EHsc)
    target_compile_definitions(grandmp_native_player_input_experimental PRIVATE
        GRANDMP_CPLAYER_VARIANT=1 GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=1
        GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1 GRANDMP_NATIVE_INPUT_REPLAY_BUILD=1)
    target_link_libraries(grandmp_native_player_input_experimental PRIVATE GrandMP::Shared grandmp_windows_detection bcrypt user32 ws2_32)
    target_link_options(grandmp_native_player_input_experimental PRIVATE /INCREMENTAL:NO
        "/MAP:${CMAKE_CURRENT_BINARY_DIR}/native-player-input/$<CONFIG>/GrandMPNativePlayerExperimental.map")
    set_target_properties(grandmp_native_player_input_experimental PROPERTIES PREFIX "" SUFFIX ".asi"
        OUTPUT_NAME "GrandMPNativePlayerExperimental"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-player-input"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-player-input")
endif()

# Alpha 0.9: same factory/replay, plus read-only local capture and UDP emission.
# Independent OFF gate and isolated output; never co-install with Alpha 0.8.
option(GRANDMP_BUILD_NATIVE_PLAYER_SYNC_EXPERIMENTAL "Expose isolated bidirectional local-input player ASI" OFF)
option(GRANDMP_ENABLE_BACKGROUND_GAMEPLAY "Enable authenticated background gameplay in the Alpha 0.9 sync ASI only" OFF)
option(GRANDMP_ENABLE_BOOTSTRAP_DIAGNOSTIC "Expose value-only bootstrap evidence in the isolated sync ASI" OFF)
if(GRANDMP_ENABLE_BOOTSTRAP_DIAGNOSTIC AND NOT GRANDMP_BUILD_NATIVE_PLAYER_SYNC_EXPERIMENTAL)
    message(FATAL_ERROR "Bootstrap diagnostics require the isolated native player sync target")
endif()
if(GRANDMP_ENABLE_BACKGROUND_GAMEPLAY AND NOT GRANDMP_BUILD_NATIVE_PLAYER_SYNC_EXPERIMENTAL)
    message(FATAL_ERROR "Background gameplay requires the isolated native player sync target")
endif()
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND GRANDMP_BUILD_NATIVE_PLAYER_SYNC_EXPERIMENTAL)
    set(native_sync_def tools/NativePlayerSyncExperimental.def)
    if(GRANDMP_ENABLE_BOOTSTRAP_DIAGNOSTIC)
        set(native_sync_def tools/NativePlayerSyncBootstrapDiagnostic.def)
    endif()
    add_library(grandmp_native_player_sync_experimental MODULE EXCLUDE_FROM_ALL
        tools/ped_smoke.cpp ${native_sync_def}
        src/native_second_player.cpp src/remote_player_binding.cpp src/observer_network.cpp)
    target_compile_features(grandmp_native_player_sync_experimental PRIVATE cxx_std_20)
    target_include_directories(grandmp_native_player_sync_experimental PRIVATE include)
    target_compile_options(grandmp_native_player_sync_experimental PRIVATE /JMC- /W4 /WX /EHsc)
    target_compile_definitions(grandmp_native_player_sync_experimental PRIVATE
        GRANDMP_CPLAYER_VARIANT=1 GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=1
        GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1 GRANDMP_NATIVE_INPUT_REPLAY_BUILD=1 GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD=1)
    if(GRANDMP_ENABLE_BACKGROUND_GAMEPLAY)
        target_compile_definitions(grandmp_native_player_sync_experimental PRIVATE GRANDMP_BACKGROUND_GAMEPLAY_BUILD=1)
    endif()
    if(GRANDMP_ENABLE_BOOTSTRAP_DIAGNOSTIC)
        target_compile_definitions(grandmp_native_player_sync_experimental PRIVATE GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD=1)
    endif()
    target_link_libraries(grandmp_native_player_sync_experimental PRIVATE GrandMP::Shared grandmp_windows_detection bcrypt user32 ws2_32)
    target_link_options(grandmp_native_player_sync_experimental PRIVATE /INCREMENTAL:NO
        "/MAP:${CMAKE_CURRENT_BINARY_DIR}/native-player-sync/$<CONFIG>/GrandMPNativePlayerSyncExperimental.map")
    set_target_properties(grandmp_native_player_sync_experimental PROPERTIES PREFIX "" SUFFIX ".asi"
        OUTPUT_NAME "GrandMPNativePlayerSyncExperimental"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-player-sync"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-player-sync")
endif()

# Slot-1-only candidate. This option exposes a CLOSED static library, never an ASI.
option(GRANDMP_BUILD_NATIVE_SECOND_PLAYER_SLOT "Expose closed Hoodlum second-player lifecycle candidate" OFF)
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND GRANDMP_BUILD_NATIVE_SECOND_PLAYER_SLOT)
    add_library(grandmp_native_second_player_slot STATIC EXCLUDE_FROM_ALL src/native_second_player.cpp)
    target_compile_features(grandmp_native_second_player_slot PUBLIC cxx_std_20)
    target_compile_options(grandmp_native_second_player_slot PRIVATE /W4 /WX /EHsc)
    target_compile_definitions(grandmp_native_second_player_slot PRIVATE GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=0)
    target_include_directories(grandmp_native_second_player_slot PUBLIC include)
    target_link_libraries(grandmp_native_second_player_slot PUBLIC GrandMP::Shared)
endif()

# OFF by default. An executable pipeline inspector, never a physical ASI binding.
option(GRANDMP_BUILD_REMOTE_PLAYER_CONTROLLER_HOST "Expose the isolated CPlayerPed input pipeline host (NO native execution)" OFF)
if(GRANDMP_BUILD_REMOTE_PLAYER_CONTROLLER_HOST)
    add_executable(grandmp_remote_player_controller_host EXCLUDE_FROM_ALL
        tools/remote_player_controller_host.cpp src/observer_network.cpp)
    target_include_directories(grandmp_remote_player_controller_host PRIVATE include)
    target_link_libraries(grandmp_remote_player_controller_host PRIVATE GrandMP::Shared ws2_32 bcrypt)
endif()
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

# Exclusive Alpha 0.8 READ-ONLY task diagnostic variant. Old targets never select
# this define. Expose only; compilation/installation require separate review.
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    option(GRANDMP_BUILD_REMOTE_PED_TASK_DIAGNOSTIC_ENABLED "Expose the separately reviewed one-walk task diagnostic target" OFF)
    option(GRANDMP_ENABLE_PASSIVE_MOVEMENT_DIAGNOSTIC "Include passive pre-world samples ONLY in the task diagnostic ASI" OFF)
    if(GRANDMP_ENABLE_PASSIVE_MOVEMENT_DIAGNOSTIC AND NOT GRANDMP_BUILD_REMOTE_PED_TASK_DIAGNOSTIC_ENABLED)
        message(FATAL_ERROR "Passive movement diagnostics require the exclusive task diagnostic target")
    endif()
    if(GRANDMP_BUILD_REMOTE_PED_TASK_DIAGNOSTIC_ENABLED)
        add_library(grandmp_remote_ped_task_diagnostic_enabled MODULE EXCLUDE_FROM_ALL
            tools/ped_smoke.cpp tools/remote_ped_move.def src/observer_network.cpp)
        target_compile_features(grandmp_remote_ped_task_diagnostic_enabled PRIVATE cxx_std_20)
        target_compile_definitions(grandmp_remote_ped_task_diagnostic_enabled PRIVATE
            GRANDMP_REMOTE_PED_VARIANT=1 GRANDMP_REMOTE_PED_MOVE_VARIANT=1 GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD=1
            GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD=$<BOOL:${GRANDMP_ENABLE_PASSIVE_MOVEMENT_DIAGNOSTIC}>
            GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1 GRANDMP_REMOTE_PED_PHYSICAL_BUILD=1 GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD=1)
        target_compile_options(grandmp_remote_ped_task_diagnostic_enabled PRIVATE /JMC-)
        target_link_options(grandmp_remote_ped_task_diagnostic_enabled PRIVATE /INCREMENTAL:NO
            "/MAP:$<TARGET_FILE_DIR:grandmp_remote_ped_task_diagnostic_enabled>/GrandMPRemotePedMoveExperimental.map")
        target_link_libraries(grandmp_remote_ped_task_diagnostic_enabled PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
        set_target_properties(grandmp_remote_ped_task_diagnostic_enabled PROPERTIES PREFIX "" OUTPUT_NAME "GrandMPRemotePedMoveExperimental" SUFFIX ".asi"
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-task-diagnostic-enabled"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-task-diagnostic-enabled"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows/remote-ped-task-diagnostic-enabled")
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

# Separate slot-1 native factory; no SCM, civil PED backend, replay or GoToPoint.
option(GRANDMP_BUILD_NATIVE_PLAYER_EXPERIMENTAL "Expose isolated Hoodlum CPlayerPed factory ASI" OFF)
if(WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4 AND GRANDMP_BUILD_NATIVE_PLAYER_EXPERIMENTAL)
    add_library(grandmp_native_player_experimental MODULE EXCLUDE_FROM_ALL
        tools/ped_smoke.cpp tools/NativePlayerExperimental.def
        src/native_second_player.cpp src/remote_player_binding.cpp src/observer_network.cpp)
    target_compile_features(grandmp_native_player_experimental PRIVATE cxx_std_20)
    target_include_directories(grandmp_native_player_experimental PRIVATE include)
    target_compile_options(grandmp_native_player_experimental PRIVATE /JMC- /W4 /WX /EHsc)
    target_compile_definitions(grandmp_native_player_experimental PRIVATE
        GRANDMP_CPLAYER_VARIANT=1 GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=1
        GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1)
    target_link_libraries(grandmp_native_player_experimental PRIVATE GrandMP::Shared grandmp_windows_detection bcrypt user32 ws2_32)
    target_link_options(grandmp_native_player_experimental PRIVATE /INCREMENTAL:NO
        "/MAP:${CMAKE_CURRENT_BINARY_DIR}/native-player/$<CONFIG>/GrandMPNativePlayerExperimental.map")
    set_target_properties(grandmp_native_player_experimental PROPERTIES PREFIX "" SUFFIX ".asi"
        OUTPUT_NAME "GrandMPNativePlayerExperimental"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-player"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/native-player")
endif()
