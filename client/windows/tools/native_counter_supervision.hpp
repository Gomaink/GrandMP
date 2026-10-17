#pragma once

#include "native_counter_preflight.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>

namespace grandmp::windows {
// Fixed budget from resume, never extended by late or repeated focus changes.
// Only the physical supervisor uses this policy; no authorization is implied.
class NativeCounterFocusObservation final {
public:
    static constexpr std::uint64_t budgetMs = 10000, observationMs = 1500;
    enum class State { waiting, observing, complete, no_focus_timeout, focus_timeout, focus_lost };
    NativeCounterFocusObservation(std::uint32_t pid, std::uint64_t resumedAt) noexcept
        : pid_(pid), resumedAt_(resumedAt) {}
    State sample(std::uint64_t now, std::uint32_t foregroundPid) noexcept {
        if (finished()) return state_;
        if (focusedAt_ && foregroundPid != pid_) return state_ = State::focus_lost;
        const auto elapsed = now - resumedAt_;
        if (!focusedAt_ && elapsed >= budgetMs) return state_ = State::no_focus_timeout;
        if (!focusedAt_ && pid_ && foregroundPid == pid_) {
            focusedAt_ = now;
            state_ = State::observing;
        }
        if (focusedAt_ && elapsed <= budgetMs && now - *focusedAt_ >= observationMs)
            return state_ = State::complete;
        if (elapsed >= budgetMs) return state_ = State::focus_timeout;
        return state_;
    }
    [[nodiscard]] bool finished() const noexcept {
        return state_ != State::waiting && state_ != State::observing;
    }
    [[nodiscard]] bool callbackObserved(std::uint64_t before, std::uint64_t after) const noexcept {
        return state_ == State::complete && after > before;
    }
    [[nodiscard]] const char* result() const noexcept {
        switch (state_) {
        case State::waiting: return "waiting";
        case State::observing: return "observing";
        case State::complete: return "complete";
        case State::no_focus_timeout: return "no_focus_timeout";
        case State::focus_timeout: return "focus_timeout";
        case State::focus_lost: return "focus_lost";
        }
        return "unknown";
    }
private:
    std::uint32_t pid_;
    std::uint64_t resumedAt_;
    std::optional<std::uint64_t> focusedAt_;
    State state_ = State::waiting;
};

// Explicit user approval on 2026-10-16, only for the scripts/ experimental
// admitted below. This reference is independent of CLI input and measured
// copies; a rebuild never approves its new digest automatically.
inline constexpr std::array<std::byte, 32> nativeCounterSmokeApprovedExperimentalDigest{
    std::byte{0xAC}, std::byte{0x0B}, std::byte{0x83}, std::byte{0x9D},
    std::byte{0x5E}, std::byte{0x52}, std::byte{0x4D}, std::byte{0xDA},
    std::byte{0xD6}, std::byte{0x59}, std::byte{0x58}, std::byte{0x8E},
    std::byte{0xC9}, std::byte{0xB9}, std::byte{0x89}, std::byte{0x4C},
    std::byte{0x2B}, std::byte{0x0E}, std::byte{0xDC}, std::byte{0x1A},
    std::byte{0x07}, std::byte{0xFE}, std::byte{0xA8}, std::byte{0xEB},
    std::byte{0x57}, std::byte{0xDB}, std::byte{0x23}, std::byte{0xE2},
    std::byte{0xFD}, std::byte{0xCC}, std::byte{0x84}, std::byte{0x6C}};

[[nodiscard]] inline bool nativeCounterSmokeDigestApproved(
    const std::array<std::byte, 32>& digest) noexcept {
    return std::any_of(nativeCounterSmokeApprovedExperimentalDigest.begin(),
        nativeCounterSmokeApprovedExperimentalDigest.end(), [](std::byte value) { return value != std::byte{}; }) &&
        digest == nativeCounterSmokeApprovedExperimentalDigest;
}

// Inputs must be canonical paths, resolved from the actual opened files.
// This is scope admission only: it never grants a write or a debugger stop.
[[nodiscard]] inline bool nativeCounterSmokeScope(
    std::wstring_view executable, std::wstring_view module,
    const NativeCounterFileEvidence& image,
    const std::array<std::byte, 32>& moduleDigest,
    const std::array<std::byte, 32>& expectedDigest) noexcept {
    return executable == L"D:/Jogos/GrandMP-Smoke/gta_sa.exe" &&
        module == L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPNativeCounterExperimental.asi" &&
        recognizeGameVersion(image.inspection, image.digest) == GameVersion::gta_sa_10us_hoodlum &&
        nativeCounterSmokeDigestApproved(expectedDigest) &&
        moduleDigest == expectedDigest;
}
}  // namespace grandmp::windows
