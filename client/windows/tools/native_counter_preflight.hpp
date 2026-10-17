#pragma once

#include "grandmp/windows/executable_detection.hpp"
#include "native_counter_admission.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <vector>

namespace grandmp::windows {
struct NativeCounterFileEvidence {
    std::vector<std::byte> bytes;
    ExecutableInspection inspection;
    std::array<std::byte, 32> digest{};
    std::uint32_t imageBase = 0;
    std::uint32_t imageSize = 0;
};

inline constexpr std::uint32_t nativeCounterAdmissionFileLimit = 32U * 1024 * 1024;
// Inventory reads remain bounded; no caller can request a larger allocation.
inline constexpr std::uint32_t nativeCounterInventoryFileLimit = 64U * 1024 * 1024;

// Read-only local file inspection. No process is started, attached or written.
// Executable/ASI admission retains its previous limit unless explicitly narrowed
// or expanded for the debugger's module inventory, up to the hard cap above.
[[nodiscard]] std::optional<NativeCounterFileEvidence> readNativeCounterFile(
    const std::filesystem::path& path,
    std::uint32_t maximumBytes = nativeCounterAdmissionFileLimit);

// Resolve only the bounded DATA export of the admitted local file.
[[nodiscard]] std::optional<std::uint32_t> nativeCounterReadyRva(
    const NativeCounterFileEvidence& file) noexcept;
[[nodiscard]] std::optional<std::uint32_t> nativeCounterWrapperRva(
    const NativeCounterFileEvidence& file) noexcept;

struct NativeCounterPhysicalSnapshot {
    std::uint32_t processId = 0;
    std::uint32_t executableBase = 0;
    std::uint32_t moduleBase = 0;
    std::array<std::byte, 32> loadedModuleFileDigest{};
    NativeCounterReadyV1 first{};
    NativeCounterReadyV1 second{};
    std::span<const std::uint8_t> callBytes;
};

enum class NativeCounterPreflightResult {
    unknown_executable, incompatible_base, incompatible_module,
    unstable_snapshot, not_ready, incompatible_identity, incompatible_call,
    evidence_consistent_read_only,
};

// The collector must independently associate loadedModuleFileDigest and both
// snapshots with the loaded image. This checks supplied evidence, not ownership
// of a debugger event. Even complete evidence NEVER authorizes writing.
[[nodiscard]] NativeCounterPreflightResult inspectNativeCounterPhysicalPreflight(
    const NativeCounterFileEvidence& executable,
    const NativeCounterFileEvidence& admittedExperimental,
    const NativeCounterPhysicalSnapshot& snapshot) noexcept;
struct NativeCounterAdmissionDryRun {
    NativeCounterPreflightResult evidence;
    std::optional<NativeCounterCallPlan> plan;
};

// Complete physical evidence is required before planning. Residency-only READY
// remains not_ready; no preparation export, writer or remote code is invoked.
[[nodiscard]] NativeCounterAdmissionDryRun dryRunNativeCounterAdmission(
    const NativeCounterFileEvidence& executable,
    const NativeCounterFileEvidence& admittedExperimental,
    const NativeCounterPhysicalSnapshot& snapshot) noexcept;
// Build a candidate from the worker's private INITIALIZING/PIN snapshot.
// No input is mutated. The caller owns CPU/page checks and release-publication;
// the existing physical preflight and CALL planner admit the candidate.
[[nodiscard]] std::optional<NativeCounterReadyV1> prepareNativeCounterHoodlumDescriptor(
    const NativeCounterFileEvidence& executable,
    const NativeCounterFileEvidence& admittedExperimental,
    const NativeCounterPhysicalSnapshot& initializing) noexcept;
inline constexpr bool nativeCounterGtaWriteEnabled = false;
}  // namespace grandmp::windows
