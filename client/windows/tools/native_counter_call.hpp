#pragma once

#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <span>

namespace grandmp::windows {
using NativeCounterCall = std::array<std::uint8_t, 5>;

// Deliberately restricted lab profile: no wraparound rel32 addresses. x86 can
// encode more by modulo arithmetic; this experiment conservatively rejects it.
[[nodiscard]] inline std::optional<NativeCounterCall> encodeNativeCounterCall(
    std::uint64_t source, std::uint64_t target) noexcept {
    if (source == 0 || target == 0 || source > UINT32_MAX - 5ULL || target > UINT32_MAX)
        return std::nullopt;
    const auto delta = static_cast<std::int64_t>(target) -
                       static_cast<std::int64_t>(source + 5);
    if (delta < INT32_MIN || delta > INT32_MAX) return std::nullopt;
    NativeCounterCall bytes{0xE8};
    const auto displacement = static_cast<std::uint32_t>(delta);
    for (unsigned index = 0; index < 4; ++index)
        bytes[index + 1] = static_cast<std::uint8_t>(displacement >> (index * 8));
    return bytes;
}

[[nodiscard]] inline std::optional<std::uint32_t> decodeNativeCounterCall(
    std::uint32_t source, std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() != 5 || bytes[0] != 0xE8 || source > UINT32_MAX - 5U)
        return std::nullopt;
    std::uint32_t encoded = 0;
    for (unsigned index = 0; index < 4; ++index)
        encoded |= std::uint32_t{bytes[index + 1]} << (index * 8);
    const auto displacement = static_cast<std::int32_t>(encoded);
    const auto target = static_cast<std::int64_t>(source) + 5 + displacement;
    if (target <= 0 || target > UINT32_MAX) return std::nullopt;
    return static_cast<std::uint32_t>(target);
}
}  // namespace grandmp::windows
