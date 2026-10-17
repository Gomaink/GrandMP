#pragma once

#include <cstdint>
#include <type_traits>

// Wire data read from the experimental module's PE DATA export externally.
// It contains x86 addresses only. No function invocation is needed to read it.
namespace grandmp::windows {
inline constexpr std::uint32_t nativeCounterReadyMagic = 0x47524D50;
inline constexpr std::uint32_t nativeCounterReadyVersion = 1;
inline constexpr char nativeCounterReadyMarker[] = "GRANDMP_NATIVE_COUNTER_READY_V1";
enum class NativeCounterReadyState : std::uint32_t {
    dormant = 0, uninitialized = 0, requested = 1, ready = 2,
    disabled = 3, shutdown = 3, failed = 4, initializing = 5
};
struct NativeCounterReadyV1 {
    std::uint32_t magic;
    std::uint32_t interfaceVersion;
    std::uint32_t size;
    std::uint32_t processId;
    std::uint32_t moduleBase;
    std::uint32_t moduleSize;
    std::uint32_t state;
    std::uint32_t residencyConfirmed;
    std::uint32_t wrapper;
    std::uint32_t originalTarget;
    std::uint32_t callsite;
    std::uint32_t counterAddress;
    std::uint32_t descriptorAddress;
    std::uint32_t installed;
    std::uint32_t wrapperCapable;
    std::uint32_t shutdownLogical;
    std::uint32_t threadId;
    std::uint32_t originalBytesLow;
    std::uint32_t originalBytesHigh;
    std::uint32_t reserved;
};
static_assert(sizeof(NativeCounterReadyV1) == 80);
static_assert(std::is_standard_layout_v<NativeCounterReadyV1>);
}  // namespace grandmp::windows
