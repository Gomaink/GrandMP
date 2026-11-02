#pragma once
#include <array>
#include <cstdint>
namespace grandmp::windows::cleo_bootstrap {
// Hoodlum opcode 03A4 folds ASCII A-Z across eight bytes, then copies to
// CRunningScript+8 (4846C0..4846E0). Accept only this script's native image.
inline constexpr char nativeScriptName[8]{'g','m','s','c','m','0','9','\0'};
inline constexpr bool scriptNameMatches(const char (&name)[8]) noexcept {
    for(unsigned i=0;i<8;++i)if(name[i]!=nativeScriptName[i])return false;
    return true;
}
// Log bytes in display order, retaining the eighth byte. No C-string traversal.
inline constexpr std::uint64_t scriptNameBytes(const char (&name)[8]) noexcept {
    std::uint64_t value=0;
    for(unsigned i=0;i<8;++i)value=(value<<8)|static_cast<unsigned char>(name[i]);
    return value;
}
// Official CLEO4 4.4.4 SHA 748EA0B3...CAA08. extraOpcodeHandler restores
// EBP and tail-jumps the registered stdcall handler, replacing its stack arg.
// CRunningScript::Process saves EBX/ESI; CLEO's EF00 wrapper saves ESI.
// CTheScripts::Process saves four registers and reserves sixteen local bytes;
// its caller must be the original CGame::Process CALL at 0x53BFC7.
// Offsets are return-stack words, not a speculative search for return addresses.
inline bool schedulerStack(const std::array<std::uint32_t,16>& stack,
                           std::uintptr_t cleoBase,std::uintptr_t script) noexcept {
    return cleoBase && cleoBase<=UINT32_MAX-0xEF63 && script &&
        stack[0]==0x469FF7 && stack[1]==script &&
        stack[4]==cleoBase+0xEF63 && stack[6]==0x46A220 && stack[15]==0x53BFCC;
}
}
