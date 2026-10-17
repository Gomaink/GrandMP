#pragma once

#include "grandmp/windows/executable_detection.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>

namespace grandmp::windows {

// Physical Hoodlum evidence: 0x53E981 -> 0x53BEE0, captured 20261012-223706.
// plugin-sdk 15f15b60bbf74c106e1b496ff92c98764abf4605, shared/Events.h.
inline constexpr std::array<std::byte, 5> nativeCounterOriginalCall{
    std::byte{0xE8}, std::byte{0x5A}, std::byte{0xD5}, std::byte{0xFF}, std::byte{0xFF}};

enum class NativeCounterGateResult : unsigned int {
    disabled,
    unknown_executable,
    incompatible_bytes,
    installation_unavailable,
};

// Read-only preflight over evidence supplied by the caller. Even correct
// evidence never authorizes installation: no GTA writer exists in this slice.
[[nodiscard]] inline NativeCounterGateResult inspectNativeCounterGate(
    bool optIn, const ExecutableInspection& inspection,
    std::span<const std::byte, 32> digest, std::span<const std::byte> callBytes) noexcept {
    if (!optIn) return NativeCounterGateResult::disabled;
    if (recognizeGameVersion(inspection, digest) != GameVersion::gta_sa_10us_hoodlum)
        return NativeCounterGateResult::unknown_executable;
    if (callBytes.size() != nativeCounterOriginalCall.size() ||
        !std::equal(callBytes.begin(), callBytes.end(), nativeCounterOriginalCall.begin()))
        return NativeCounterGateResult::incompatible_bytes;
    return NativeCounterGateResult::installation_unavailable;
}

}  // namespace grandmp::windows
