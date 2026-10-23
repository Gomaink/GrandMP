#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include "grandmp/windows/remote_player_hoodlum.hpp"
#include <array>
#include <cstring>
#include <initializer_list>
#include <limits>

namespace grandmp::windows::remote_player_binding {
namespace {
bool u32(std::span<const std::byte> file, std::size_t at, std::uint32_t& value) noexcept {
    if (at>file.size() || sizeof(value)>file.size()-at) return false;
    std::memcpy(&value,file.data()+at,sizeof(value));return true;
}
// Translate a preferred VA to raw file bytes. Never treat a mapped image as a file.
bool matches(std::span<const std::byte> file,std::uint32_t va,
             std::initializer_list<unsigned char> bytes) noexcept {
    std::uint32_t pe=0,base=0;
    if (!u32(file,0x3c,pe) || pe>file.size() || file.size()-pe<24 ||
        !u32(file,static_cast<std::size_t>(pe)+52,base) || va<base) return false;
    std::uint16_t count=0,opt=0;
    std::memcpy(&count,file.data()+pe+6,2);std::memcpy(&opt,file.data()+pe+20,2);
    const auto table=static_cast<std::size_t>(pe)+24+opt;
    const auto rva=va-base;
    for (unsigned n=0;n<count;++n) {
        const auto section=table+40*n;
        std::uint32_t address=0,size=0,raw=0;
        if (!u32(file,section+12,address) || !u32(file,section+16,size) || !u32(file,section+20,raw)) return false;
        if (rva<address || rva-address>=size) continue;
        const auto delta=rva-address;
        auto at=static_cast<std::size_t>(raw)+delta;
        if (bytes.size()>size-delta || at>file.size() || bytes.size()>file.size()-at) return false;
        for (auto b:bytes) if (file[at++]!=static_cast<std::byte>(b)) return false;
        return true;
    }
    return false;
}
}
HoodlumContract inspectHoodlumPlayerContract(std::span<const std::byte> file) noexcept {
    HoodlumContract result;
    const auto pe=inspectExecutable(file);
    if (pe.status!=PeStatus::pe32_i386 || file.size()>std::numeric_limits<ULONG>::max()) return result;
    std::array<std::byte,32> digest{};
    if (BCryptHash(BCRYPT_SHA256_ALG_HANDLE,nullptr,0,
            reinterpret_cast<PUCHAR>(const_cast<std::byte*>(file.data())),static_cast<ULONG>(file.size()),
            reinterpret_cast<PUCHAR>(digest.data()),static_cast<ULONG>(digest.size()))<0) return result;
    result.exactExecutable=recognizeGameVersion(pe,digest)==GameVersion::gta_sa_10us_hoodlum;
    if (!result.exactExecutable) return result;
    // Small Hoodlum evidence windows, not signatures authorizing native execution.
    result.playerConstructorWindows=matches(file,0x60D5EE,{0xc7,0x06,0x68,0xd1,0x86,0x00}) &&
        matches(file,0x60D77E,{0xc2,0x08,0x00});
    result.slotFactoryWindows=matches(file,0x60D7D9,{0x69,0xf6,0x90,0x01,0x00,0x00}) &&
        matches(file,0x60D7EA,{0x89,0x86,0x98,0xcd,0xb7,0x00});
    result.slotDestructorWindows=matches(file,0x60950A,{0x8b,0x11,0x6a,0x01,0xff,0x12}) &&
        matches(file,0x609517,{0xc3});
    result.processControlVtable=matches(file,0x86D190,{0x90,0xea,0x60,0x00});
    return result;
}
} // namespace grandmp::windows::remote_player_binding
