#pragma once

#include "native_counter_call.hpp"
#include "native_counter_ready.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>

namespace grandmp::windows {
struct NativeCounterCallPlan {
    std::uint32_t callsite;
    std::uint32_t originalTarget;
    std::uint32_t wrapper;
    NativeCounterCall original;
    NativeCounterCall replacement;
};

// Pure planning shared by the read-only Hoodlum admission and lab writer.
// The caller must independently validate image/module identity and bindings.
// A plan carries bytes only, never a debug stop or permission to write.
[[nodiscard]] inline std::optional<NativeCounterCallPlan> planNativeCounterCall(
    const NativeCounterReadyV1& ready, std::span<const std::uint8_t> observed) noexcept {
    if (ready.magic != nativeCounterReadyMagic || ready.interfaceVersion != nativeCounterReadyVersion ||
        ready.size != sizeof(ready) || ready.state != static_cast<std::uint32_t>(NativeCounterReadyState::ready) ||
        ready.residencyConfirmed != 1 || ready.wrapperCapable != 1 || ready.installed != 0 ||
        ready.shutdownLogical != 0 || ready.reserved != 0 || ready.wrapper == ready.originalTarget)
        return {};
    const auto original = encodeNativeCounterCall(ready.callsite, ready.originalTarget);
    const auto replacement = encodeNativeCounterCall(ready.callsite, ready.wrapper);
    if (!original || !replacement || observed.size() != original->size() ||
        !std::equal(observed.begin(), observed.end(), original->begin()) ||
        decodeNativeCounterCall(ready.callsite, observed) != ready.originalTarget) return {};
    std::uint32_t low = 0;
    std::memcpy(&low, original->data(), sizeof(low));
    if (ready.originalBytesLow != low || ready.originalBytesHigh != (*original)[4]) return {};
    return NativeCounterCallPlan{ready.callsite, ready.originalTarget, ready.wrapper, *original, *replacement};
}
}  // namespace grandmp::windows
