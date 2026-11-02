#pragma once
#include "ped_smoke_code_identity.hpp"
#include <filesystem>

namespace grandmp::windows::ped_smoke {
struct AdmissionFailure {
    const char* stage = "none";
    const char* category = "none";
    DWORD error = 0, requested = 0, transferred = 0;
    unsigned long status = 0;
    int filesystemError = 0;
    bool filesystemSystem = false;
    CodeIdentityFailure code{};
};
inline bool admissionRefuse(AdmissionFailure& f, const char* stage, const char* category,
                            DWORD error = 0) noexcept {
    if (!std::strcmp(f.stage, "none")) { f.stage = stage; f.category = category; f.error = error; }
    return false;
}
// Same canonical operation as the runtime previously used. The named boundary
// lets offline tests inject filesystem failures without creating game files.
inline std::filesystem::path canonicalForAdmission(const std::filesystem::path& path) {
    return std::filesystem::canonical(path);
}
inline bool formatAdmissionFailure(char* output, std::size_t capacity, const AdmissionFailure& f) noexcept {
    if (!output || !capacity) return false;
    const int n = std::snprintf(output, capacity,
        "admission_check stage=%s category=%s win32=%lu ntstatus=%08lX filesystem_error=%d filesystem_system=%u requested=%lu transferred=%lu",
        f.stage, f.category, f.error, f.status, f.filesystemError,
        f.filesystemSystem ? 1u : 0u, f.requested, f.transferred);
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { output[0] = '\0'; return false; }
    return true;
}
} // namespace grandmp::windows::ped_smoke
