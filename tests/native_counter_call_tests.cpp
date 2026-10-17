#include "native_counter_call.hpp"

#include <array>
#include <cstdint>

int main() {
    using namespace grandmp::windows;
    const NativeCounterCall expected{0xE8, 0x5A, 0xD5, 0xFF, 0xFF};
    const auto encoded = encodeNativeCounterCall(0x53E981, 0x53BEE0);
    if (!encoded || *encoded != expected || decodeNativeCounterCall(0x53E981, expected) != 0x53BEE0)
        return 1;
    if (encodeNativeCounterCall(0, 1) || encodeNativeCounterCall(1, 0) ||
        encodeNativeCounterCall(UINT32_MAX - 3ULL, 1) ||
        encodeNativeCounterCall(0x400000, 0xF0000000) ||
        encodeNativeCounterCall(0x400000, 0x100000000ULL)) return 2;
    if (!encodeNativeCounterCall(0x80000000, 5) ||
        !encodeNativeCounterCall(1, 0x80000005)) return 3;
    auto incompatible = expected;
    incompatible[0] = 0xE9;
    if (decodeNativeCounterCall(0x53E981, incompatible) ||
        decodeNativeCounterCall(0x53E981, std::span(expected).first(4)) ||
        decodeNativeCounterCall(UINT32_MAX - 3U, expected)) return 4;
    return 0;
}
