#pragma once

#include "grandmp/windows/native_player_input.hpp"
#include "ped_smoke_runtime_read.hpp"
#include <algorithm>
#include <windows.h>

namespace grandmp::windows::remote_player_binding::replay {
namespace ps=ped_smoke;
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1 && \
    defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1 && \
    defined(GRANDMP_PED_SMOKE_PHYSICAL_BUILD) && GRANDMP_PED_SMOKE_PHYSICAL_BUILD == 1
inline constexpr bool executionApproved=true;
#else
inline constexpr bool executionApproved=false;
#endif
// Exact Hoodlum image only. Instruction evidence: docs/native-onfoot-replay.md.
inline constexpr std::uint32_t original=0x60EA90, cell=0x86D190;
inline constexpr std::uint32_t cameraHeading=0xB6F178, weaponCameraMode=0xB6F858;
inline constexpr std::array<std::uint32_t,3> callers{0x568561,0x568642,0x5686F4};

// Zelda computes atan2(-LR,-UD) - cameraHeading. Rotate the input basis,
// rather than replacing focus/camera/aim globals. No world pose/velocity writer.
inline bool cameraRelative(core::PlayerControls input,float remoteView,float liveView,
                           core::PlayerControls& output) noexcept {
    core::OnFootState checked{};checked.controls=input;checked.viewHeading=remoteView;
    if (!core::validOnFoot(checked) || !std::isfinite(liveView)) return false;
    const float length=std::hypot(input.right,input.forward);
    if (length>1) { input.right/=length;input.forward/=length; }
    const float angle=liveView-remoteView,c=std::cos(angle),s=std::sin(angle);
    output={std::clamp(input.right*c-input.forward*s,-1.f,1.f),
            std::clamp(input.right*s+input.forward*c,-1.f,1.f),input.buttons};
    return true;
}

// The caller authenticates ownership and the exact address before lending it.
// Kept separate to exercise the real Win32 writer/finally in a disposable IMAGE.
inline bool writePadStates(std::uint32_t at,void* imageBase,bool context,
                           const OnFootPadStates& values) noexcept {
    if (!context) return false;
    MEMORY_BASIC_INFORMATION page{};
    if (!VirtualQuery(reinterpret_cast<void*>(at),&page,sizeof(page)) || page.State!=MEM_COMMIT ||
        page.Type!=MEM_IMAGE || page.AllocationBase!=imageBase ||
        (page.Protect!=PAGE_READWRITE && page.Protect!=PAGE_WRITECOPY) ||
        at>UINT32_MAX-0x60 || at+0x60>reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize) return false;
    std::array<std::byte,0x60> bytes{},actual{};
    std::memcpy(bytes.data(),values.current.data(),0x30);
    std::memcpy(bytes.data()+0x30,values.previous.data(),0x30);
    SIZE_T done=0;
    const bool written=WriteProcessMemory(GetCurrentProcess(),reinterpret_cast<void*>(at),
        bytes.data(),bytes.size(),&done)!=FALSE && done==bytes.size();
    return written && ps::readMemory(at,actual.data(),actual.size()).complete(actual.size()) && actual==bytes;
}

// One publication, one removal, no retry. A failed cleanup does NOT erase the
// fact that the cell was published. Resident target survives outstanding calls.
class HookCell final {
public:
    bool installed() const noexcept { return published_; }
    DWORD error() const noexcept { return error_; }
    bool install(std::uint32_t* cellAddress,std::uint32_t expected,std::uint32_t replacement,
                 void* imageBase) noexcept {
        if (attempted_) return false;
        attempted_=true;cell_=cellAddress;original_=expected;hook_=replacement;base_=imageBase;
        if (!page() || !matches(original_)) return false;
        return exchange(original_,hook_,true);
    }
    bool remove() noexcept {
        if (!published_) return !failed_;
        if (removalAttempted_) return false;
        removalAttempted_=true;
        if (!page() || !matches(hook_)) return false;
        return exchange(hook_,original_,false);
    }
private:
    bool matches(std::uint32_t expected) noexcept {
        std::uint32_t actual=0;
        const auto read=ps::readMemory(static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(cell_)),&actual,4);
        if (!read.complete(4)) { error_=read.error;return false; }
        return actual==expected;
    }
    bool page() noexcept {
        MEMORY_BASIC_INFORMATION page{};
        if (!VirtualQuery(cell_,&page,sizeof(page))) { error_=GetLastError();return false; }
        const auto at=reinterpret_cast<std::uintptr_t>(cell_);
        return page.AllocationBase==base_ && page.State==MEM_COMMIT && page.Protect==PAGE_READONLY &&
            page.Type==MEM_IMAGE &&
            at>=reinterpret_cast<std::uintptr_t>(page.BaseAddress) &&
            at+4<=reinterpret_cast<std::uintptr_t>(page.BaseAddress)+page.RegionSize;
    }
    bool exchange(std::uint32_t from,std::uint32_t to,bool publish) noexcept {
        DWORD old=0;
        if (!VirtualProtect(cell_,4,PAGE_READWRITE,&old)) { error_=GetLastError();failed_=true;return false; }
        bool swapped=false;
        if (old==PAGE_READONLY) {
            swapped=static_cast<std::uint32_t>(InterlockedCompareExchange(
                reinterpret_cast<volatile LONG*>(cell_),static_cast<LONG>(to),static_cast<LONG>(from)))==from;
            if (swapped) published_=publish;
        }
        DWORD ignored=0;
        const bool restored=VirtualProtect(cell_,4,old,&ignored)!=FALSE;
        if (!restored) error_=GetLastError();
        const bool okay=swapped && restored && page() && matches(to);
        failed_=failed_ || !okay;return okay;
    }
    std::uint32_t* cell_=nullptr;
    std::uint32_t original_=0,hook_=0;
    void* base_=nullptr;
    DWORD error_=ERROR_SUCCESS;
    bool attempted_=false,published_=false,removalAttempted_=false,failed_=false;
};
} // namespace grandmp::windows::remote_player_binding::replay
