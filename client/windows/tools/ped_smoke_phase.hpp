#pragma once
#include "ped_smoke_addresses.hpp"
#include <cstddef>
#include <cstring>
#include <limits>

namespace grandmp::windows::ped_smoke {
using PhaseOriginal = void(__cdecl*)();
// Immutable before publication. The lab supplies a disposable original instead.
extern const PhaseOriginal originalPhase;
void __cdecl phaseBody(std::uintptr_t caller) noexcept;

using CallBytes = std::array<std::byte, 5>;
inline constexpr CallBytes originalCall{std::byte{0xE8}, std::byte{0xFF},
    std::byte{0xE2}, std::byte{0x1F}, std::byte{0x00}};
inline bool encodePhaseCall(std::uintptr_t target, CallBytes& bytes) noexcept {
    const auto offset = std::int64_t{target} - (std::int64_t{phaseCall} + 5);
    if (offset < (std::numeric_limits<std::int32_t>::min)() || offset > (std::numeric_limits<std::int32_t>::max)()) return false;
    const auto relative = static_cast<std::int32_t>(offset);
    bytes[0] = std::byte{0xE8};
    std::memcpy(bytes.data() + 1, &relative, sizeof(relative));
    return true;
}

// CALL void __cdecl(): keep its native return address and forward exactly once.
// Preserve flags, all GPRs and x87/MMX/SSE across the experimental body, including
// paths which do nothing. FXSAVE requires a 16-byte-aligned 512-byte area/FXSR.
__declspec(naked) inline void phaseThunk() {
    __asm {
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, 0FFFFFFF0h
        mov [esp + 512], eax
        fxsave [esp]
        fninit
        mov dword ptr [esp + 516], 1F80h
        ldmxcsr [esp + 516]
        cld
        push dword ptr [eax + 36]
        call phaseBody
        add esp, 4
        fxrstor [esp]
        mov esp, [esp + 512]
        popad
        popfd
        jmp dword ptr [originalPhase]
    }
}
} // namespace grandmp::windows::ped_smoke
