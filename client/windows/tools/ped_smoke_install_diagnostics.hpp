#pragma once

#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace grandmp::windows::ped_smoke {

enum class InstallFailure : std::uint32_t {
    none,
    f6_timeout_120s,
    window_thread_unavailable,
    module_pin_failed,
    module_pin_mismatch,
    set_windows_hook_failed,
    post_thread_message_failed,
    callback_timeout_5000ms,
    candidate_physical_gate_closed,
    candidate_game_thread_unavailable,
    candidate_window_thread_mismatch,
    candidate_wrong_thread,
    candidate_cpu_fxsr_unavailable,
    candidate_sse2_unavailable,
    candidate_player_not_ready,
    candidate_streaming_not_ready,
    candidate_code_identity_mismatch,
    candidate_call_encoding_failed,
    candidate_virtual_protect_failed,
    candidate_unexpected_page_protection,
    candidate_initial_protection_restore_failed,
    candidate_instruction_cache_flush_failed,
    candidate_protection_restore_failed,
    candidate_post_publish_code_mismatch,
    candidate_code_write_failed,
    candidate_code_rollback_failed,
    unhook_failed
};

struct InstallDiagnostic {
    InstallFailure failure = InstallFailure::none;
    std::uint32_t win32Error = 0;
    bool hasWin32Error = false;
};

constexpr InstallDiagnostic f6WaitResult(bool detected) noexcept {
    return detected ? InstallDiagnostic{} : InstallDiagnostic{InstallFailure::f6_timeout_120s, 0, false};
}

struct InstallOutcome {
    InstallDiagnostic primary{};
    // A failed PostThreadMessage may be followed by a valid callback from an
    // already queued window message. Preserve that existing success behavior.
    InstallDiagnostic bootstrapWarning{};
    // Keep candidate cleanup (for example, restoring page protection) separate
    // from removing the bootstrap hook so neither failure masks the other.
    InstallDiagnostic candidateCleanup{};
    InstallDiagnostic unhookFailure{};

    constexpr bool succeeded() const noexcept {
        return primary.failure == InstallFailure::none &&
            candidateCleanup.failure == InstallFailure::none &&
            unhookFailure.failure == InstallFailure::none;
    }
};

constexpr const char* installFailureReason(InstallFailure failure) noexcept {
    switch (failure) {
    case InstallFailure::none: return "none";
    case InstallFailure::f6_timeout_120s: return "f6_timeout_120s";
    case InstallFailure::window_thread_unavailable: return "window_thread_unavailable";
    case InstallFailure::module_pin_failed: return "module_pin_failed";
    case InstallFailure::module_pin_mismatch: return "module_pin_mismatch";
    case InstallFailure::set_windows_hook_failed: return "set_windows_hook_failed";
    case InstallFailure::post_thread_message_failed: return "post_thread_message_failed";
    case InstallFailure::callback_timeout_5000ms: return "callback_timeout_5000ms";
    case InstallFailure::candidate_physical_gate_closed: return "install_candidate_physical_gate_closed";
    case InstallFailure::candidate_game_thread_unavailable: return "install_candidate_game_thread_unavailable";
    case InstallFailure::candidate_window_thread_mismatch: return "install_candidate_window_thread_mismatch";
    case InstallFailure::candidate_wrong_thread: return "install_candidate_wrong_thread";
    case InstallFailure::candidate_cpu_fxsr_unavailable: return "install_candidate_cpu_fxsr_unavailable";
    case InstallFailure::candidate_sse2_unavailable: return "install_candidate_sse2_unavailable";
    case InstallFailure::candidate_player_not_ready: return "install_candidate_player_not_ready";
    case InstallFailure::candidate_streaming_not_ready: return "install_candidate_streaming_not_ready";
    case InstallFailure::candidate_code_identity_mismatch: return "install_candidate_code_identity_mismatch";
    case InstallFailure::candidate_call_encoding_failed: return "install_candidate_call_encoding_failed";
    case InstallFailure::candidate_virtual_protect_failed: return "install_candidate_virtual_protect_failed";
    case InstallFailure::candidate_unexpected_page_protection: return "install_candidate_unexpected_page_protection";
    case InstallFailure::candidate_initial_protection_restore_failed: return "install_candidate_initial_protection_restore_failed";
    case InstallFailure::candidate_instruction_cache_flush_failed: return "install_candidate_instruction_cache_flush_failed_module_pinned_no_engine_dispatch";
    case InstallFailure::candidate_protection_restore_failed: return "install_candidate_protection_restore_failed_module_pinned_no_engine_dispatch";
    case InstallFailure::candidate_post_publish_code_mismatch: return "install_candidate_post_publish_code_mismatch_module_pinned_no_engine_dispatch";
    case InstallFailure::candidate_code_write_failed: return "install_candidate_code_write_failed_module_pinned_no_engine_dispatch";
    case InstallFailure::candidate_code_rollback_failed: return "install_candidate_code_rollback_failed_module_pinned_no_engine_dispatch";
    case InstallFailure::unhook_failed: return "bootstrap_unhook_failed_module_pinned";
    }
    return "install_diagnostic_unknown_failure";
}

inline bool formatInstallDiagnostic(char* output, std::size_t capacity,
    const InstallDiagnostic& diagnostic) noexcept {
    if (!output || capacity == 0 || diagnostic.failure == InstallFailure::none) return false;
    const int count = diagnostic.hasWin32Error
        ? std::snprintf(output, capacity, "%s win32_error=%lu", installFailureReason(diagnostic.failure),
            static_cast<unsigned long>(diagnostic.win32Error))
        : std::snprintf(output, capacity, "%s", installFailureReason(diagnostic.failure));
    return count >= 0 && static_cast<std::size_t>(count) < capacity;
}

} // namespace grandmp::windows::ped_smoke
