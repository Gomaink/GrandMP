if(BUILD_TESTING)
    add_executable(grandmp_remote_player_controller_tests remote_player_controller_tests.cpp)
    target_link_libraries(grandmp_remote_player_controller_tests PRIVATE grandmp_server_sessions)
    if(WIN32)
        target_sources(grandmp_remote_player_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
        target_include_directories(grandmp_remote_player_controller_tests PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/include)
        target_link_libraries(grandmp_remote_player_controller_tests PRIVATE ws2_32 bcrypt)
        add_test(NAME remote_player_controller_sender COMMAND grandmp_remote_player_controller_tests sender)
    endif()
    foreach(controller_case wire server continuous stale lifecycle thread end_reasons)
        add_test(NAME remote_player_controller_${controller_case} COMMAND grandmp_remote_player_controller_tests ${controller_case})
    endforeach()
    add_executable(grandmp_core_tests core_tests.cpp)
    target_link_libraries(grandmp_core_tests PRIVATE GrandMP::Shared)

    add_executable(grandmp_protocol_tests protocol_tests.cpp)
    target_link_libraries(grandmp_protocol_tests PRIVATE GrandMP::Shared)

    add_executable(grandmp_remote_roster_tests remote_roster_tests.cpp)
    target_link_libraries(grandmp_remote_roster_tests PRIVATE GrandMP::Shared)
    add_test(NAME remote_roster_reordering_and_loss COMMAND grandmp_remote_roster_tests)

    add_executable(grandmp_remote_player_scene_tests remote_player_scene_tests.cpp)
    target_link_libraries(grandmp_remote_player_scene_tests PRIVATE GrandMP::Shared)
    add_test(NAME remote_scene_lifecycle COMMAND grandmp_remote_player_scene_tests lifecycle)
    add_test(NAME remote_scene_interpolation COMMAND grandmp_remote_player_scene_tests interpolation)
    add_test(NAME remote_scene_teleport COMMAND grandmp_remote_player_scene_tests teleport)
    add_test(NAME remote_scene_reset COMMAND grandmp_remote_player_scene_tests reset)

    add_executable(grandmp_remote_player_adapter_tests remote_player_adapter_tests.cpp)
    target_link_libraries(grandmp_remote_player_adapter_tests PRIVATE GrandMP::Shared)
    foreach(adapter_case scene ordering session availability shutdown thread reentrant_reset concurrent queue entity_limit failure persistent_create persistent_update shutdown_recovery cleanup_failure persistent_remove remove_recovery backend_unlocked reuse_during_remove reuse_after_create_despawn)
        add_test(NAME remote_adapter_${adapter_case}
                 COMMAND grandmp_remote_player_adapter_tests ${adapter_case})
    endforeach()

    add_executable(grandmp_server_replication_state_tests server_replication_state_tests.cpp)
    target_link_libraries(grandmp_server_replication_state_tests PRIVATE grandmp_server_sessions)
    add_test(NAME server_replication_snapshot_and_replacement COMMAND grandmp_server_replication_state_tests)

    add_executable(grandmp_server_session_tests server_session_tests.cpp)
    target_link_libraries(grandmp_server_session_tests PRIVATE grandmp_server_sessions)

    add_executable(grandmp_server_loopback_tests server_loopback_tests.cpp)
    target_link_libraries(grandmp_server_loopback_tests PRIVATE grandmp_server_sessions)
    if(WIN32)
        target_link_libraries(grandmp_server_loopback_tests PRIVATE ws2_32)
    endif()

    add_test(NAME protocol_round_trip COMMAND grandmp_protocol_tests round_trip)
    add_test(NAME protocol_wire_format COMMAND grandmp_protocol_tests wire_format)
    add_test(NAME protocol_malformed COMMAND grandmp_protocol_tests malformed)
    add_test(NAME protocol_invalid_fields COMMAND grandmp_protocol_tests invalid_fields)
    add_test(NAME server_sessions_and_sequence COMMAND grandmp_server_session_tests sessions)
    add_test(NAME server_timeout_and_disconnect COMMAND grandmp_server_session_tests timeout)
    add_test(NAME server_reconnect_and_capacity COMMAND grandmp_server_session_tests restart)
    add_test(NAME server_two_clients_loopback COMMAND grandmp_server_loopback_tests)
    add_test(NAME server_help COMMAND grandmp_server --help)
    add_test(NAME synthetic_client_help COMMAND grandmp_synthetic_client --help)

    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        if(TARGET grandmp_remote_player_controller_host)
            add_test(NAME remote_player_controller_real_udp
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/remote_player_controller_loopback.py
                    $<TARGET_FILE:grandmp_server> $<TARGET_FILE:grandmp_remote_player_controller_host> $<TARGET_FILE:grandmp_synthetic_client>)
            set_tests_properties(remote_player_controller_real_udp PROPERTIES TIMEOUT 25)
            add_test(NAME remote_player_controller_demo_welcome_timeout
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/remote_player_controller_loopback.py
                    --welcome-timeout $<TARGET_FILE:grandmp_synthetic_client>)
            set_tests_properties(remote_player_controller_demo_welcome_timeout PROPERTIES TIMEOUT 18)
        endif()
        add_test(NAME server_replication_real_udp
            COMMAND ${Python3_EXECUTABLE}
                ${CMAKE_CURRENT_SOURCE_DIR}/replication_loopback.py
                $<TARGET_FILE:grandmp_server>)
        if(WIN32 AND TARGET grandmp_remote_scene_host)
            add_test(NAME server_to_remote_scene_adapter
                COMMAND ${Python3_EXECUTABLE}
                    ${CMAKE_CURRENT_SOURCE_DIR}/remote_scene_loopback.py
                    $<TARGET_FILE:grandmp_server>
                    $<TARGET_FILE:grandmp_remote_scene_host>
                    $<TARGET_FILE:grandmp_synthetic_client>)
            set_tests_properties(server_to_remote_scene_adapter PROPERTIES TIMEOUT 18)
        endif()
    endif()

    add_test(NAME version_identity COMMAND grandmp_core_tests version)
    add_test(NAME logger_dispatch COMMAND grandmp_core_tests logger)
    add_test(NAME client_adapter_contract COMMAND grandmp_core_tests adapter)
endif()

if(BUILD_TESTING AND TARGET grandmp_ped_smoke)
    add_executable(grandmp_ped_smoke_tests ped_smoke_tests.cpp)
    target_compile_features(grandmp_ped_smoke_tests PRIVATE cxx_std_20)
    target_compile_options(grandmp_ped_smoke_tests PRIVATE /JMC-)
    foreach(ped_case waiting one_ped reentrant removal identity_lost failures wrong_thread cancel post_create phase_abi install_diagnostics spawn_diagnostics spawn_nonfinite lifecycle_loss terminal_removal f8_edges
            move_command move_arrival move_loss move_removal move_gates move_geometry move_failures move_abi)
        add_test(NAME ped_smoke_${ped_case} COMMAND grandmp_ped_smoke_tests ${ped_case})
    endforeach()
    add_test(NAME ped_smoke_initialization COMMAND grandmp_ped_smoke_tests initialization $<TARGET_FILE:grandmp_ped_smoke>)
    set_tests_properties(ped_smoke_initialization PROPERTIES TIMEOUT 10)
    if(TARGET grandmp_ped_smoke_enabled)
        add_executable(grandmp_ped_smoke_enabled_tests EXCLUDE_FROM_ALL ped_smoke_tests.cpp)
        target_compile_features(grandmp_ped_smoke_enabled_tests PRIVATE cxx_std_20)
        target_compile_definitions(grandmp_ped_smoke_enabled_tests PRIVATE GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1)
        target_compile_options(grandmp_ped_smoke_enabled_tests PRIVATE /JMC-)
        foreach(ped_case waiting one_ped reentrant removal identity_lost failures wrong_thread cancel post_create phase_abi spawn_diagnostics spawn_nonfinite lifecycle_loss terminal_removal f8_edges
                move_command move_arrival move_loss move_removal move_gates move_geometry move_failures move_abi)
            add_test(NAME ped_smoke_enabled_${ped_case} COMMAND grandmp_ped_smoke_enabled_tests ${ped_case})
        endforeach()
        add_test(NAME ped_smoke_enabled_initialization
            COMMAND grandmp_ped_smoke_enabled_tests initialization_enabled $<TARGET_FILE:grandmp_ped_smoke_enabled>)
        set_tests_properties(ped_smoke_enabled_initialization PROPERTIES TIMEOUT 10)
    endif()
    if(TARGET grandmp_ped_smoke_move_enabled)
        add_executable(grandmp_ped_smoke_move_enabled_tests EXCLUDE_FROM_ALL ped_smoke_tests.cpp)
        target_compile_features(grandmp_ped_smoke_move_enabled_tests PRIVATE cxx_std_20)
        target_compile_definitions(grandmp_ped_smoke_move_enabled_tests PRIVATE
            GRANDMP_PED_SMOKE_PHYSICAL_BUILD=1 GRANDMP_PED_MOVE_PHYSICAL_BUILD=1)
        target_compile_options(grandmp_ped_smoke_move_enabled_tests PRIVATE /JMC-)
        foreach(ped_case waiting one_ped reentrant removal identity_lost failures wrong_thread cancel post_create phase_abi spawn_diagnostics spawn_nonfinite lifecycle_loss terminal_removal f8_edges
                move_command move_arrival move_loss move_removal move_gates move_geometry move_failures move_abi)
            add_test(NAME ped_smoke_move_enabled_${ped_case} COMMAND grandmp_ped_smoke_move_enabled_tests ${ped_case})
        endforeach()
        add_test(NAME ped_smoke_move_enabled_initialization
            COMMAND grandmp_ped_smoke_move_enabled_tests initialization_enabled $<TARGET_FILE:grandmp_ped_smoke_move_enabled>)
        set_tests_properties(ped_smoke_move_enabled_initialization PROPERTIES TIMEOUT 10)
    endif()
    # Pure gate tests, including the future (1,1) combination: no ASI loading,
    # native addresses, allocation, hook, or engine code is linked into these hosts.
    foreach(ped_spawn_gate 0 1)
        foreach(ped_move_gate 0 1)
            set(ped_gate_target grandmp_ped_build_gates_${ped_spawn_gate}${ped_move_gate})
            add_executable(${ped_gate_target} ped_smoke_build_gates_tests.cpp)
            target_compile_features(${ped_gate_target} PRIVATE cxx_std_20)
            target_compile_definitions(${ped_gate_target} PRIVATE
                GRANDMP_PED_SMOKE_PHYSICAL_BUILD=${ped_spawn_gate}
                GRANDMP_PED_MOVE_PHYSICAL_BUILD=${ped_move_gate}
                GRANDMP_EXPECT_SPAWN_GATE=${ped_spawn_gate} GRANDMP_EXPECT_MOVE_GATE=${ped_move_gate})
            add_test(NAME ped_smoke_build_gates_${ped_spawn_gate}${ped_move_gate} COMMAND ${ped_gate_target})
        endforeach()
    endforeach()
endif()

if(BUILD_TESTING AND WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    if(TARGET grandmp_remote_ped_experimental)
        add_executable(grandmp_remote_ped_tests remote_ped_tests.cpp
            ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
        target_compile_features(grandmp_remote_ped_tests PRIVATE cxx_std_20)
        target_include_directories(grandmp_remote_ped_tests PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/include)
        target_link_libraries(grandmp_remote_ped_tests PRIVATE GrandMP::Shared ws2_32 bcrypt)
        foreach(remote_ped_case join reentry limit leave cancel reconnect stale ownership failure thread invalid isolation registry receiver code)
            add_test(NAME remote_ped_${remote_ped_case} COMMAND grandmp_remote_ped_tests ${remote_ped_case})
        endforeach()
        # Pure gate matrix: no ASI/engine code, including future (1,1).
        foreach(remote_spawn_gate 0 1)
            foreach(remote_gate 0 1)
                set(remote_gate_target grandmp_remote_ped_gates_${remote_spawn_gate}${remote_gate})
                add_executable(${remote_gate_target} remote_ped_build_gates_tests.cpp)
                target_compile_features(${remote_gate_target} PRIVATE cxx_std_20)
                target_link_libraries(${remote_gate_target} PRIVATE GrandMP::Shared)
                target_compile_definitions(${remote_gate_target} PRIVATE
                    GRANDMP_PED_SMOKE_PHYSICAL_BUILD=${remote_spawn_gate}
                    GRANDMP_REMOTE_PED_PHYSICAL_BUILD=${remote_gate}
                    GRANDMP_EXPECT_SPAWN_GATE=${remote_spawn_gate} GRANDMP_EXPECT_REMOTE_GATE=${remote_gate})
                add_test(NAME remote_ped_gates_${remote_spawn_gate}${remote_gate} COMMAND ${remote_gate_target})
            endforeach()
        endforeach()
        add_executable(grandmp_remote_ped_runtime_tests remote_ped_runtime_tests.cpp
            ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
        target_compile_features(grandmp_remote_ped_runtime_tests PRIVATE cxx_std_20)
        target_compile_options(grandmp_remote_ped_runtime_tests PRIVATE /JMC-)
        target_link_libraries(grandmp_remote_ped_runtime_tests PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
        foreach(remote_runtime_case receiver code)
            add_test(NAME remote_ped_runtime_${remote_runtime_case} COMMAND grandmp_remote_ped_runtime_tests ${remote_runtime_case})
            set_tests_properties(remote_ped_runtime_${remote_runtime_case} PROPERTIES TIMEOUT 10)
        endforeach()
        add_test(NAME remote_ped_loader COMMAND grandmp_remote_ped_tests loader $<TARGET_FILE:grandmp_remote_ped_experimental>)
        set_tests_properties(remote_ped_loader PROPERTIES TIMEOUT 10)
        if(TARGET grandmp_remote_ped_enabled)
            add_test(NAME remote_ped_loader_enabled COMMAND grandmp_remote_ped_tests loader_enabled $<TARGET_FILE:grandmp_remote_ped_enabled>)
            set_tests_properties(remote_ped_loader_enabled PROPERTIES TIMEOUT 10)
        endif()
        if(Python3_Interpreter_FOUND)
            add_test(NAME remote_ped_real_loopback
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/remote_ped_loopback.py
                    $<TARGET_FILE:grandmp_server> $<TARGET_FILE:grandmp_remote_ped_tests> $<TARGET_FILE:grandmp_synthetic_client>)
            set_tests_properties(remote_ped_real_loopback PROPERTIES TIMEOUT 18)
        endif()
    endif()
    add_executable(grandmp_observer_network_tests
        observer_network_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
    target_compile_features(grandmp_observer_network_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_observer_network_tests PRIVATE GrandMP::Shared
        grandmp_position_observer_reader ws2_32 bcrypt)
    add_test(NAME observer_network_loopback COMMAND grandmp_observer_network_tests)
    add_executable(grandmp_observer_adapter_pipeline_tests
        observer_adapter_pipeline_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
    target_compile_features(grandmp_observer_adapter_pipeline_tests PRIVATE cxx_std_20)
    target_include_directories(grandmp_observer_adapter_pipeline_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/client/windows/include)
    target_link_libraries(grandmp_observer_adapter_pipeline_tests PRIVATE GrandMP::Shared ws2_32 bcrypt)
    add_test(NAME remote_scene_adapter_pipeline COMMAND grandmp_observer_adapter_pipeline_tests)
    set_tests_properties(remote_scene_adapter_pipeline PROPERTIES TIMEOUT 15)
    add_test(NAME position_observer_help COMMAND grandmp_position_observer --help)
    add_executable(grandmp_position_observer_platform_tests
        position_observer_platform_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
    target_compile_features(grandmp_position_observer_platform_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_position_observer_platform_tests PRIVATE
        grandmp_position_observer_reader grandmp_windows_detection
        GrandMP::Shared bcrypt ws2_32)
    add_test(NAME position_observer_win32_platform
             COMMAND grandmp_position_observer_platform_tests)
    add_test(NAME window_observer_help COMMAND grandmp_window_observer --help)
    add_test(NAME callback_lifecycle_host_smoke
             COMMAND grandmp_callback_lifecycle_host --ticks 16)
    set_tests_properties(callback_lifecycle_host_smoke PROPERTIES
        PASS_REGULAR_EXPRESSION "ticks=16 callbacks=16 state=stopped unload_safe=1")

    add_library(grandmp_timer_probe MODULE timer_probe_dll.cpp)
    target_compile_features(grandmp_timer_probe PRIVATE cxx_std_20)
    add_executable(grandmp_timer_host_tests timer_host_tests.cpp)
    target_compile_features(grandmp_timer_host_tests PRIVATE cxx_std_20)
    add_dependencies(grandmp_timer_host_tests grandmp_timer_probe)
    add_test(NAME timer_host_message_queue
             COMMAND grandmp_timer_host_tests $<TARGET_FILE:grandmp_timer_probe>)

    add_executable(grandmp_asi_entry_tests asi_entry_tests.cpp)
    target_compile_features(grandmp_asi_entry_tests PRIVATE cxx_std_20)
    add_dependencies(grandmp_asi_entry_tests grandmp_windows)
    add_test(NAME asi_entry_once COMMAND grandmp_asi_entry_tests $<TARGET_FILE:grandmp_windows>)

    add_executable(grandmp_module_pin_win32_tests module_pin_win32_tests.cpp)
    target_compile_features(grandmp_module_pin_win32_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_module_pin_win32_tests PRIVATE grandmp_windows_detection)
    add_dependencies(grandmp_module_pin_win32_tests grandmp_windows)
    add_test(NAME module_pin_win32_process_residency
             COMMAND grandmp_module_pin_win32_tests $<TARGET_FILE:grandmp_windows>)

    add_test(NAME asi_bootstrap_dynamic_module_lifetime
             COMMAND grandmp_asi_bootstrap_host $<TARGET_FILE:grandmp_asi_bootstrap_experimental>)
    set_tests_properties(asi_bootstrap_dynamic_module_lifetime PROPERTIES TIMEOUT 15
        PASS_REGULAR_EXPRESSION "bootstrap_host=pass entry=concurrent deferred=once logical_shutdown=pass module_resident=1")
endif()

if(BUILD_TESTING AND WIN32)
    add_executable(grandmp_asi_bootstrap_tests asi_bootstrap_tests.cpp)
    target_link_libraries(grandmp_asi_bootstrap_tests PRIVATE grandmp_asi_bootstrap)
    foreach(bootstrap_case initialization partial_failure logical_shutdown concurrent_entry concurrent_deferred)
        add_test(NAME asi_bootstrap_${bootstrap_case}
                 COMMAND grandmp_asi_bootstrap_tests ${bootstrap_case})
        set_tests_properties(asi_bootstrap_${bootstrap_case} PROPERTIES TIMEOUT 15)
    endforeach()

    if(TARGET grandmp_callback_dll_lifetime_host)
        foreach(lifetime_case normal active direct_active shutdown_active init_failure rollback_failure cleanup_failure reentrant cycles concurrent_unload concurrent_initialize)
            add_test(NAME callback_dll_lifetime_${lifetime_case}
                COMMAND grandmp_callback_dll_lifetime_host ${lifetime_case}
                    $<TARGET_FILE:grandmp_callback_lifetime_module>)
            set_tests_properties(callback_dll_lifetime_${lifetime_case} PROPERTIES TIMEOUT 15)
        endforeach()
    endif()

    add_executable(grandmp_position_observer_tests position_observer_tests.cpp)
    target_compile_features(grandmp_position_observer_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_position_observer_tests PRIVATE grandmp_position_observer_reader)

    add_test(NAME observer_process_absent COMMAND grandmp_position_observer_tests absent)
    add_test(NAME observer_matrix_position COMMAND grandmp_position_observer_tests matrix)
    add_test(NAME observer_fallback_position COMMAND grandmp_position_observer_tests fallback)
    add_test(NAME observer_invalid_pointer COMMAND grandmp_position_observer_tests pointer)
    add_test(NAME observer_read_failure COMMAND grandmp_position_observer_tests read_failure)
    add_test(NAME observer_partial_read COMMAND grandmp_position_observer_tests partial)
    add_test(NAME observer_non_finite COMMAND grandmp_position_observer_tests non_finite)
    add_test(NAME observer_changed_pointer COMMAND grandmp_position_observer_tests changing)
    add_test(NAME observer_process_exit COMMAND grandmp_position_observer_tests process_exit)
    add_test(NAME observer_read_only_access COMMAND grandmp_position_observer_tests read_only)

    add_executable(grandmp_windows_detection_tests windows_detection_tests.cpp)
    target_link_libraries(grandmp_windows_detection_tests PRIVATE grandmp_windows_detection)

    add_executable(grandmp_player_position_tests player_position_tests.cpp)
    target_compile_features(grandmp_player_position_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_player_position_tests PRIVATE grandmp_windows_detection)

    add_executable(grandmp_hook_lifecycle_tests hook_lifecycle_tests.cpp)
    target_compile_features(grandmp_hook_lifecycle_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_hook_lifecycle_tests PRIVATE grandmp_windows_detection)

    add_executable(grandmp_callback_lifecycle_tests callback_lifecycle_tests.cpp)
    target_compile_features(grandmp_callback_lifecycle_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_callback_lifecycle_tests PRIVATE grandmp_windows_lifecycle)
    foreach(lifecycle_case initialization partial reentrant shutdown concurrency shutdown_race cleanup_failure unregister)
        add_test(NAME callback_lifecycle_${lifecycle_case}
                 COMMAND grandmp_callback_lifecycle_tests ${lifecycle_case})
    endforeach()

    add_executable(grandmp_module_residency_tests module_residency_tests.cpp)
    target_compile_features(grandmp_module_residency_tests PRIVATE cxx_std_20)
    target_link_libraries(grandmp_module_residency_tests PRIVATE grandmp_windows_detection)

    add_test(NAME pe32_i386_detection COMMAND grandmp_windows_detection_tests pe32)
    add_test(NAME malformed_executable_rejection COMMAND grandmp_windows_detection_tests malformed)
    add_test(NAME pe32_plus_rejection COMMAND grandmp_windows_detection_tests pe32plus)
    add_test(NAME unknown_game_rejection COMMAND grandmp_windows_detection_tests unknown)
    add_test(NAME hoodlum_fingerprint_match COMMAND grandmp_windows_detection_tests hoodlum)
    add_test(NAME modified_executable_rejection COMMAND grandmp_windows_detection_tests modified)
    add_test(NAME position_unknown_game_rejection COMMAND grandmp_player_position_tests unknown)
    add_test(NAME position_player_absent COMMAND grandmp_player_position_tests absent)
    add_test(NAME position_coordinate_validation COMMAND grandmp_player_position_tests coordinates)
    add_test(NAME position_log_frequency COMMAND grandmp_player_position_tests frequency)
    add_test(NAME position_integration_unavailable COMMAND grandmp_player_position_tests unavailable)
    add_test(NAME hook_install_once COMMAND grandmp_hook_lifecycle_tests install)
    add_test(NAME hook_identity_and_preconditions COMMAND grandmp_hook_lifecycle_tests identity)
    add_test(NAME hook_unexpected_bytes COMMAND grandmp_hook_lifecycle_tests bytes)
    add_test(NAME hook_logical_disable COMMAND grandmp_hook_lifecycle_tests disable)
    add_test(NAME hook_foreign_patch COMMAND grandmp_hook_lifecycle_tests foreign)
    add_test(NAME hook_partial_failure COMMAND grandmp_hook_lifecycle_tests failure)
    add_test(NAME hook_object_lifetime COMMAND grandmp_hook_lifecycle_tests lifetime)
    add_test(NAME hook_inflight_and_reentrancy COMMAND grandmp_hook_lifecycle_tests inflight)
    add_test(NAME hook_destruction_guard COMMAND grandmp_hook_lifecycle_tests destruction)
    add_test(NAME module_pin_once COMMAND grandmp_module_residency_tests once)
    add_test(NAME module_pin_reject_unknown COMMAND grandmp_module_residency_tests unknown)
    add_test(NAME module_pin_reject_other_module COMMAND grandmp_module_residency_tests module)
    add_test(NAME module_pin_failure COMMAND grandmp_module_residency_tests failure)
    add_test(NAME module_pin_invalid_identity COMMAND grandmp_module_residency_tests identity)
    add_test(NAME module_pin_no_cleanup COMMAND grandmp_module_residency_tests cleanup)
endif()

if(BUILD_TESTING AND TARGET grandmp_native_counter_host)
    foreach(counter_case gate once partial concurrent residence shutdown forwarding reentrant cancel shutdown_race)
        add_test(NAME native_counter_${counter_case}
            COMMAND grandmp_native_counter_host $<TARGET_FILE:grandmp_native_counter_lab> ${counter_case})
        set_tests_properties(native_counter_${counter_case} PROPERTIES TIMEOUT 15)
    endforeach()
endif()

if(BUILD_TESTING AND TARGET grandmp_native_counter_experimental)
    add_test(NAME native_counter_inert_asi
        COMMAND grandmp_native_counter_host $<TARGET_FILE:grandmp_native_counter_experimental> inert)
    set_tests_properties(native_counter_inert_asi PROPERTIES TIMEOUT 15)
    foreach(asi_counter_case asi_ready asi_concurrent asi_not_requested asi_partial asi_shutdown asi_shutdown_race)
        add_test(NAME native_counter_${asi_counter_case}
            COMMAND grandmp_native_counter_host $<TARGET_FILE:grandmp_native_counter_experimental> ${asi_counter_case})
        set_tests_properties(native_counter_${asi_counter_case} PROPERTIES TIMEOUT 15)
    endforeach()
    foreach(async_case async_ready async_concurrent async_release)
        add_test(NAME native_counter_${async_case}
            COMMAND grandmp_native_counter_host $<TARGET_FILE:grandmp_native_counter_experimental> ${async_case})
        set_tests_properties(native_counter_${async_case} PROPERTIES TIMEOUT 15)
    endforeach()
    foreach(async_failure_case async_unload async_shutdown async_thread_failure async_pin_failure async_reference_failure)
        add_test(NAME native_counter_${async_failure_case}
            COMMAND grandmp_native_counter_host $<TARGET_FILE:grandmp_native_counter_lab> ${async_failure_case})
        set_tests_properties(native_counter_${async_failure_case} PROPERTIES TIMEOUT 15)
    endforeach()
    if(Python3_Interpreter_FOUND AND TARGET grandmp_native_counter_diagnostic)
        add_test(NAME native_counter_async_diagnostic
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native_counter_diagnostic_test.py
                $<TARGET_FILE:grandmp_native_counter_host> $<TARGET_FILE:grandmp_native_counter_experimental>
                $<TARGET_FILE:grandmp_native_counter_diagnostic> $<TARGET_FILE:grandmp_native_counter_lab>)
        set_tests_properties(native_counter_async_diagnostic PROPERTIES TIMEOUT 20)
    endif()
endif()

if(BUILD_TESTING AND TARGET grandmp_native_counter_preflight)
    add_executable(grandmp_native_counter_preflight_tests native_counter_preflight_tests.cpp)
    target_link_libraries(grandmp_native_counter_preflight_tests PRIVATE grandmp_native_counter_preflight)
    foreach(preflight_case file identity ready module call policy admission admission_rejections
            hoodlum_preparation hoodlum_preparation_rejections smoke_scope
            focus_success focus_timeout focus_late focus_lost focus_boundary)
        add_test(NAME native_counter_preflight_${preflight_case}
            COMMAND grandmp_native_counter_preflight_tests ${preflight_case})
    endforeach()
    if(TARGET grandmp_native_counter_experimental)
        add_dependencies(grandmp_native_counter_preflight_tests grandmp_native_counter_experimental)
        add_test(NAME native_counter_preflight_actual_hoodlum_rejected
            COMMAND grandmp_native_counter_preflight_tests actual_hoodlum_rejected
                $<TARGET_FILE:grandmp_native_counter_experimental>)
        set_tests_properties(native_counter_preflight_actual_hoodlum_rejected PROPERTIES TIMEOUT 10)
        add_test(NAME native_counter_preflight_actual_ready
            COMMAND grandmp_native_counter_preflight_tests actual_ready
                $<TARGET_FILE:grandmp_native_counter_experimental>)
        set_tests_properties(native_counter_preflight_actual_ready PROPERTIES TIMEOUT 10)
        add_test(NAME native_counter_preflight_actual_asi
            COMMAND grandmp_native_counter_preflight_tests actual_asi
                $<TARGET_FILE:grandmp_native_counter_experimental>)
    endif()
endif()

if(BUILD_TESTING)
    add_executable(grandmp_native_counter_call_tests native_counter_call_tests.cpp)
    target_compile_features(grandmp_native_counter_call_tests PRIVATE cxx_std_20)
    target_include_directories(grandmp_native_counter_call_tests PRIVATE
        ${PROJECT_SOURCE_DIR}/client/windows/tools)
    add_test(NAME native_counter_call_encoding COMMAND grandmp_native_counter_call_tests)
endif()

if(BUILD_TESTING AND TARGET grandmp_native_counter_transaction)
    add_library(grandmp_native_inventory_probe MODULE native_inventory_probe.cpp)
    target_compile_features(grandmp_native_inventory_probe PRIVATE cxx_std_20)
    set_target_properties(grandmp_native_inventory_probe PROPERTIES PREFIX ""
        OUTPUT_NAME "GrandMPNativeInventoryProbe"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/client/windows")
    add_executable(grandmp_native_counter_inventory_file_tests native_counter_inventory_file_tests.cpp)
    target_link_libraries(grandmp_native_counter_inventory_file_tests PRIVATE grandmp_native_counter_preflight)
    add_test(NAME native_counter_inventory_file_limits
        COMMAND grandmp_native_counter_inventory_file_tests $<TARGET_FILE:grandmp_native_counter_host>
            ${CMAKE_BINARY_DIR}/Testing/native-counter-inventory-files)
    set_tests_properties(native_counter_inventory_file_limits PROPERTIES TIMEOUT 45)
    add_executable(grandmp_native_counter_inventory_guard_tests native_counter_inventory_guard_tests.cpp)
    target_link_libraries(grandmp_native_counter_inventory_guard_tests PRIVATE grandmp_native_counter_preflight)
    foreach(inventory_guard_case retained_handle read_exact)
        add_test(NAME native_counter_inventory_${inventory_guard_case}
            COMMAND grandmp_native_counter_inventory_guard_tests ${inventory_guard_case}
                $<TARGET_FILE:grandmp_native_counter_host> ${CMAKE_BINARY_DIR}/Testing/native-counter-inventory-guards)
        set_tests_properties(native_counter_inventory_${inventory_guard_case} PROPERTIES TIMEOUT 15)
    endforeach()
    foreach(transaction_case success repeat unexpected_bytes third_party offset_range
            short_write protect_failure flush_failure readback_failure restore_failure commit_failure
            identity version invalid_state
            no_residence context_failure context_overlap inactive_event inspect unauthorized
            restore_success restore_repeat restore_identity restore_invalid_state restore_unexpected_bytes
            restore_context_failure restore_context_overlap restore_inactive_event
            restore_short_write restore_protect_failure restore_flush_failure restore_readback_failure
            restore_restore_failure restore_commit_failure)
        add_test(NAME native_transaction_${transaction_case}
            COMMAND grandmp_native_counter_transaction $<TARGET_FILE:grandmp_native_counter_lab> ${transaction_case})
        set_tests_properties(native_transaction_${transaction_case} PROPERTIES TIMEOUT 20)
    endforeach()
    if(Python3_Interpreter_FOUND)
        foreach(attach_case success invalid_ready missing_module wrong_identity invalid_pid
                deny_install deny_restore lost_stop detach_failure inventory_failure inspection_failure inspection_exception context_failure disconnect
                before_attach_exception before_policy_exception policy_failure after_policy_exception timeout lost_stop_disconnect)
            add_test(NAME native_attach_${attach_case}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native_counter_attach_runner.py
                    $<TARGET_FILE:grandmp_native_counter_transaction>
                    $<TARGET_FILE:grandmp_native_counter_lab> ${attach_case})
            set_tests_properties(native_attach_${attach_case} PROPERTIES TIMEOUT 25)
        endforeach()
        foreach(supervised_case success deny_install deny_restore identity invalid_state context_failure
                unexpected_bytes short_write flush_failure commit_failure fatal_write
                restore_short_write restore_identity fatal_restore_write
                threads_create threads_exit threads_churn modules_add modules_remove modules_reload
                restore_module_event_missing restore_module_changed restore_thread_event_missing restore_exit_event_missing
                restore_context_failure restore_context_overlap restore_context_win32 restore_stop_lost)
            add_test(NAME native_supervised_${supervised_case}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native_counter_supervised_runner.py
                    $<TARGET_FILE:grandmp_native_counter_transaction>
                    $<TARGET_FILE:grandmp_native_counter_lab> ${supervised_case})
            set_tests_properties(native_supervised_${supervised_case} PROPERTIES TIMEOUT 25)
        endforeach()
        foreach(transaction_fatal_case fatal_write fatal_flush fatal_protection fatal_commit
                fatal_restore_write fatal_restore_flush fatal_restore_protection fatal_restore_commit)
            add_test(NAME native_transaction_${transaction_fatal_case}
                COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/native_transaction_runner.py
                    $<TARGET_FILE:grandmp_native_counter_transaction>
                    $<TARGET_FILE:grandmp_native_counter_lab> ${transaction_fatal_case})
            set_tests_properties(native_transaction_${transaction_fatal_case} PROPERTIES TIMEOUT 20)
        endforeach()
    endif()
endif()

# Alpha 0.7, narrowly scoped task/position fakes; no native calls in gate hosts.
if(BUILD_TESTING AND TARGET grandmp_remote_ped_move_experimental)
    add_executable(grandmp_remote_ped_move_tests remote_ped_move_tests.cpp ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
    target_compile_features(grandmp_remote_ped_move_tests PRIVATE cxx_std_20)
    target_include_directories(grandmp_remote_ped_move_tests PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/include)
    target_link_libraries(grandmp_remote_ped_move_tests PRIVATE GrandMP::Shared ws2_32 bcrypt)
    foreach(move_case anchor small valid maximum oversize nonfinite height ground obstacle collision_diagnostic duplicates stale generation player leave_before leave_active ownership identity timeout arrival reconnect thread cancel_pending reentry slope ground_nan busy_task drift stale_leave identity_publication)
        add_test(NAME remote_move_${move_case} COMMAND grandmp_remote_ped_move_tests ${move_case})
    endforeach()
    add_test(NAME remote_move_loader COMMAND grandmp_remote_ped_move_tests loader $<TARGET_FILE:grandmp_remote_ped_move_experimental>)
    set_tests_properties(remote_move_loader PROPERTIES TIMEOUT 10)
    if(TARGET grandmp_remote_ped_move_enabled)
        add_test(NAME remote_move_loader_enabled COMMAND grandmp_remote_ped_move_tests loader_enabled $<TARGET_FILE:grandmp_remote_ped_move_enabled>)
        set_tests_properties(remote_move_loader_enabled PROPERTIES TIMEOUT 10)
    endif()
    foreach(spawn_gate 0 1)
        foreach(remote_gate 0 1)
            foreach(move_gate 0 1)
                set(gate_target grandmp_remote_move_gates_${spawn_gate}${remote_gate}${move_gate})
                add_executable(${gate_target} remote_ped_move_gates_tests.cpp)
                target_compile_features(${gate_target} PRIVATE cxx_std_20)
                target_link_libraries(${gate_target} PRIVATE GrandMP::Shared)
                if(spawn_gate AND remote_gate AND move_gate)
                    set(expect_move 1)
                else()
                    set(expect_move 0)
                endif()
                target_compile_definitions(${gate_target} PRIVATE GRANDMP_PED_SMOKE_PHYSICAL_BUILD=${spawn_gate}
                    GRANDMP_REMOTE_PED_PHYSICAL_BUILD=${remote_gate} GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD=${move_gate}
                    GRANDMP_EXPECT_REMOTE_MOVE_GATE=${expect_move})
                add_test(NAME remote_move_gates_${spawn_gate}${remote_gate}${move_gate} COMMAND ${gate_target})
            endforeach()
        endforeach()
    endforeach()
    if(Python3_Interpreter_FOUND)
        add_test(NAME remote_move_loopback COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/remote_ped_loopback.py
            $<TARGET_FILE:grandmp_server> $<TARGET_FILE:grandmp_remote_ped_move_tests> $<TARGET_FILE:grandmp_synthetic_client> movement)
        set_tests_properties(remote_move_loopback PROPERTIES TIMEOUT 22)
    endif()
endif()

# Alpha 0.8 Slice 2E.2: value-only frame presentation, no native binding.
if(BUILD_TESTING)
    add_executable(grandmp_remote_pose_presentation_tests remote_pose_presentation_tests.cpp)
    target_link_libraries(grandmp_remote_pose_presentation_tests PRIVATE GrandMP::Shared)
    if(MSVC)
        target_compile_options(grandmp_remote_pose_presentation_tests PRIVATE /W4 /WX)
    endif()
    foreach(pose_case positive invalid_pose request_identity stale_pose interpolated_revision
            generation owner entity snapshot stage_identity context lifetime removed collision writer
            physical sectors local_rw dirty unknown latched order postconditions nonfinite_observation isolation)
        add_test(NAME remote_pose_presentation_simulated_${pose_case}
            COMMAND grandmp_remote_pose_presentation_tests ${pose_case})
        set_tests_properties(remote_pose_presentation_simulated_${pose_case} PROPERTIES LABELS "simulation_only")
    endforeach()
endif()

# Actual registry/scope implementation in a disposable host, no GTA backend.
if(BUILD_TESTING AND WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(grandmp_remote_player_binding_tests remote_player_binding_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/remote_player_binding.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/remote_player_hoodlum.cpp)
    target_compile_options(grandmp_remote_player_binding_tests PRIVATE /W4 /WX /EHsc)
    target_link_libraries(grandmp_remote_player_binding_tests PRIVATE GrandMP::Shared grandmp_windows_detection bcrypt)
    foreach(player_binding_case gate contract partial_create absent_create pipeline freshness duplicates reentry thread ownership dispatch
            replay_contract cleanup cleanup_unknown generation capture apply exception restore seh pad_image persistent)
        add_test(NAME remote_player_binding_${player_binding_case}
            COMMAND grandmp_remote_player_binding_tests ${player_binding_case})
    endforeach()
    # Exact authorized image inspection is opt-in, read-only and never executed.
    set(GRANDMP_HOODLUM_AUDIT_FILE "" CACHE FILEPATH "Optional offline file for CPlayerPed evidence test")
    if(GRANDMP_HOODLUM_AUDIT_FILE)
        add_test(NAME remote_player_binding_hoodlum
            COMMAND grandmp_remote_player_binding_tests hoodlum "${GRANDMP_HOODLUM_AUDIT_FILE}")
    endif()
endif()

if(BUILD_TESTING AND WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(grandmp_native_second_player_tests native_second_player_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/native_second_player.cpp)
    target_compile_options(grandmp_native_second_player_tests PRIVATE /W4 /WX /EHsc)
    target_compile_definitions(grandmp_native_second_player_tests PRIVATE GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=0)
    target_link_libraries(grandmp_native_second_player_tests PRIVATE GrandMP::Shared)
    target_include_directories(grandmp_native_second_player_tests PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/include)
    foreach(slot_case windows_closed vacancy occupied context thread closed unknown_contract partial pool_full local_unknown
            partial_factory wrong_type local_mutation register_failure identity ownership handle vehicle reference
            cleanup cleanup_unknown slot_not_cleared cleanup_call_failure binding late_occupancy cleanup_replacement model
            command_encoding command_position command_refused partial_published
            group_initial group_resources group_members finish_partial collision_volume binding_partial control_auth_only)
        add_test(NAME native_second_player_${slot_case} COMMAND grandmp_native_second_player_tests ${slot_case})
    endforeach()
endif()

# Alpha 0.8 Slice 1: value-only core + closed reference adapter/fake, never an ASI.
if(BUILD_TESTING)
    add_executable(grandmp_continuous_remote_movement_tests continuous_remote_movement_tests.cpp)
    target_link_libraries(grandmp_continuous_remote_movement_tests PRIVATE GrandMP::Shared)
    foreach(continuous_case three_walks active_latest coalescing deadband sequence generation leave ownership identity
            timeout_reconnect collision assignment geometry task_release thread reentry bounded limits scene
            unknown_slot clock feedback transfer_failure
            slots_empty slots_each slots_multiple slots_partial slots_late_read slots_manager slots_owner
            slots_identity slots_context slots_engine slots_changed release_early release_unknown snapshot_race leave_release
            cleanup_pool_live cleanup_pool_changed cleanup_partial cleanup_no_return cleanup_confirmed cleanup_poisoned
            audit_pre_arrival audit_arrived_busy audit_unknown audit_generation audit_removed audit_ownership
            audit_manager_changed audit_manager_invalid audit_manager_scope audit_stale_snapshot audit_disconnect audit_thread)
        add_test(NAME continuous_move_${continuous_case}
            COMMAND grandmp_continuous_remote_movement_tests ${continuous_case})
    endforeach()
endif()

# Alpha 0.8 Slice 2D.1: closed reassignment guards; no native backend or ASI.
if(BUILD_TESTING)
    add_executable(grandmp_native_reassignment_guard_tests native_reassignment_guard_tests.cpp)
    target_link_libraries(grandmp_native_reassignment_guard_tests PRIVATE GrandMP::Shared)
    foreach(guard_case three_walks arrival_busy early_release manager_same manager_replace manager_during_probe manager_active unknown thread
            slots_before slots_after_allocate slots_after_construct slots_final manager_after_allocate manager_after_construct manager_final
            generation reference ped ownership context owner code disconnect reconnect late_callback expired contract collision terrain
            allocation allocation_identity construction task_identity ownership_after_construct context_after_construct
            generation_after_construct generation_final cancel_after_construct reentry transfer_failure replay
            collision_after_construct terrain_after_construct contract_after_construct contract_final
            audit_manager_aba audit_final_slot audit_final_manager audit_task_reuse audit_elapsed_argument
            audit_transfer_ownership audit_transfer_reentry)
        add_test(NAME reassignment_guard_${guard_case} COMMAND grandmp_native_reassignment_guard_tests ${guard_case})
    endforeach()
endif()

# Alpha 0.8 Slice 2C: bounded read-only instrumentation, fake backend only.
if(BUILD_TESTING)
    add_executable(grandmp_native_task_release_diagnostics_tests native_task_release_diagnostics_tests.cpp)
    target_link_libraries(grandmp_native_task_release_diagnostics_tests PRIVATE GrandMP::Shared)
    foreach(release_case before_task during_walk arrived_busy release_after release_before timeout partial ownership
            generation attempt authentication thread coalescing monotonic configuration format single_walk)
        add_test(NAME task_release_diag_${release_case}
            COMMAND grandmp_native_task_release_diagnostics_tests ${release_case})
    endforeach()
endif()

# Alpha 0.8 Slice 2C.1: runtime-shaped borrowed view + deferred value events, closed.
if(BUILD_TESTING)
    add_executable(grandmp_native_diagnostic_binding_tests native_diagnostic_binding_tests.cpp)
    target_link_libraries(grandmp_native_diagnostic_binding_tests PRIVATE GrandMP::Shared)
    foreach(binding_case mapping disabled initial instance context allocation generation attempt ownership authentication timeout failure copy overflow walk coalescing)
        add_test(NAME native_binding_${binding_case}
            COMMAND grandmp_native_diagnostic_binding_tests ${binding_case})
    endforeach()
endif()

# Alpha 0.8 Slice 2C.2: concrete Windows primitives and deferred sink; hosts ONLY.
if(BUILD_TESTING AND WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(grandmp_native_diagnostic_windows_tests native_diagnostic_windows_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
    target_compile_options(grandmp_native_diagnostic_windows_tests PRIVATE /JMC-)
    target_link_libraries(grandmp_native_diagnostic_windows_tests PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
    foreach(provider_case context thread ready memory partial identity runtime complete)
        add_test(NAME diagnostic_windows_${provider_case} COMMAND grandmp_native_diagnostic_windows_tests ${provider_case})
    endforeach()
    add_executable(grandmp_native_diagnostic_deferred_tests native_diagnostic_deferred_tests.cpp)
    target_link_libraries(grandmp_native_diagnostic_deferred_tests PRIVATE GrandMP::Shared)
    foreach(deferred_case disabled ordered slow closure concurrent overflow overflow_sink absence cancel producer_stop context generation identity ownership deadline clock heartbeat_ahead consumer_conflict worker worker_absence worker_residence worker_sink worker_lifetime worker_allocation_failure worker_thread_failure)
        add_test(NAME diagnostic_deferred_${deferred_case} COMMAND grandmp_native_diagnostic_deferred_tests ${deferred_case})
        set_tests_properties(diagnostic_deferred_${deferred_case} PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# Passive movement values: closed host ONLY, no ASI includes/selects this collector.
if(BUILD_TESTING AND WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(grandmp_native_movement_diagnostic_tests native_movement_diagnostic_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
    target_compile_options(grandmp_native_movement_diagnostic_tests PRIVATE /JMC-)
    target_link_libraries(grandmp_native_movement_diagnostic_tests PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
    foreach(snapshot_case closed walk context partial identity ownership values bounded deferred once format)
        add_test(NAME movement_diagnostic_${snapshot_case} COMMAND grandmp_native_movement_diagnostic_tests ${snapshot_case})
        set_tests_properties(movement_diagnostic_${snapshot_case} PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# Alpha 0.8 Slice 2C.3: same runtime compiled OFF/ON in non-GTA hosts only.
if(BUILD_TESTING AND WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    # Reuse the approved passive fixture, now through the ACTUAL resident seams/worker.
    add_executable(grandmp_native_movement_diagnostic_integration_tests
        native_movement_diagnostic_tests.cpp ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
    target_compile_definitions(grandmp_native_movement_diagnostic_integration_tests PRIVATE
        GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD=1 GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD=1)
    target_compile_options(grandmp_native_movement_diagnostic_integration_tests PRIVATE /JMC-)
    target_link_libraries(grandmp_native_movement_diagnostic_integration_tests PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
    foreach(passive_case walk context failure inflight)
        add_test(NAME passive_integration_${passive_case} COMMAND grandmp_native_movement_diagnostic_integration_tests integration_${passive_case})
        set_tests_properties(passive_integration_${passive_case} PROPERTIES TIMEOUT 15)
    endforeach()
    foreach(diag_gate 0 1)
        add_executable(grandmp_native_diagnostic_integration_${diag_gate}_tests
            native_diagnostic_integration_tests.cpp ${PROJECT_SOURCE_DIR}/client/windows/src/observer_network.cpp)
        target_compile_definitions(grandmp_native_diagnostic_integration_${diag_gate}_tests PRIVATE GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD=${diag_gate})
        target_compile_options(grandmp_native_diagnostic_integration_${diag_gate}_tests PRIVATE /JMC-)
        target_link_libraries(grandmp_native_diagnostic_integration_${diag_gate}_tests PRIVATE grandmp_windows_detection GrandMP::Shared bcrypt user32 ws2_32)
        foreach(integration_case equivalence runtime_closed)
            add_test(NAME diagnostic_integration_${diag_gate}_${integration_case} COMMAND grandmp_native_diagnostic_integration_${diag_gate}_tests ${integration_case})
        endforeach()
    endforeach()
    foreach(integration_case walk arrived_busy arrived_empty timeout ownership leave generation context stop inflight overflow absence unknown collision reconnect service_failure service_native_denied
        preparation_timeout preparation_early preparation_delayed preparation_deadline_race preparation_stop preparation_disabled preparation_closed_queue preparation_format
        physical_release_trace complete_not_empty)
        add_test(NAME diagnostic_integration_1_${integration_case} COMMAND grandmp_native_diagnostic_integration_1_tests ${integration_case})
        set_tests_properties(diagnostic_integration_1_${integration_case} PROPERTIES TIMEOUT 10)
    endforeach()
endif()

# Code identity diagnostics: offline memory/file probes only, no game or DLL execution.
if(BUILD_TESTING AND WIN32 AND MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
    add_executable(grandmp_code_identity_tests code_identity_tests.cpp)
    target_compile_features(grandmp_code_identity_tests PRIVATE cxx_std_20)
    target_compile_options(grandmp_code_identity_tests PRIVATE /W4 /WX /EHsc)
    target_link_libraries(grandmp_code_identity_tests PRIVATE bcrypt)
    add_executable(grandmp_native_player_replay_tests native_player_replay_tests.cpp
        ${PROJECT_SOURCE_DIR}/client/windows/src/remote_player_binding.cpp)
    target_compile_options(grandmp_native_player_replay_tests PRIVATE /JMC- /W4 /WX /EHsc)
    target_link_libraries(grandmp_native_player_replay_tests PRIVATE GrandMP::Shared)
    target_include_directories(grandmp_native_player_replay_tests PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/include)
    foreach(replay_case gate basis neutral invalid mismatch tamper dispatch pad_scope pad_exception pad_seh pad_context)
        add_test(NAME native_player_replay_${replay_case} COMMAND grandmp_native_player_replay_tests ${replay_case})
    endforeach()
    # Compile the actual replay runtime with ALL execution gates closed, no DLL.
    add_library(grandmp_native_player_replay_compile OBJECT EXCLUDE_FROM_ALL
        ${PROJECT_SOURCE_DIR}/client/windows/tools/ped_smoke.cpp)
    target_compile_options(grandmp_native_player_replay_compile PRIVATE /JMC- /W4 /WX /EHsc)
    target_compile_definitions(grandmp_native_player_replay_compile PRIVATE GRANDMP_CPLAYER_VARIANT=1
        GRANDMP_NATIVE_INPUT_REPLAY_BUILD=1 GRANDMP_PED_SMOKE_PHYSICAL_BUILD=0 GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=0)
    target_include_directories(grandmp_native_player_replay_compile PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/include)
    target_link_libraries(grandmp_native_player_replay_compile PRIVATE GrandMP::Shared)
    foreach(identity_case callback first read format pages)
        add_test(NAME code_identity_${identity_case} COMMAND grandmp_code_identity_tests ${identity_case})
    endforeach()
    set(GRANDMP_CODE_IDENTITY_IMAGE_FILE "" CACHE FILEPATH "Optional exact Hoodlum file for non-executing image mapping test")
    if(GRANDMP_CODE_IDENTITY_IMAGE_FILE)
        add_test(NAME code_identity_image COMMAND grandmp_code_identity_tests image "${GRANDMP_CODE_IDENTITY_IMAGE_FILE}")
    endif()
    # Compile the real CPLAYER branch with execution gates OFF. No ASI is linked.
    add_library(grandmp_native_player_identity_compile OBJECT EXCLUDE_FROM_ALL
        ${PROJECT_SOURCE_DIR}/client/windows/tools/ped_smoke.cpp)
    target_compile_features(grandmp_native_player_identity_compile PRIVATE cxx_std_20)
    target_compile_options(grandmp_native_player_identity_compile PRIVATE /JMC- /W4 /WX /EHsc)
    target_compile_definitions(grandmp_native_player_identity_compile PRIVATE
        GRANDMP_CPLAYER_VARIANT=1 GRANDMP_PED_SMOKE_PHYSICAL_BUILD=0 GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD=0)
    target_include_directories(grandmp_native_player_identity_compile PRIVATE ${PROJECT_SOURCE_DIR}/client/windows/include)
    target_link_libraries(grandmp_native_player_identity_compile PRIVATE GrandMP::Shared)
endif()
