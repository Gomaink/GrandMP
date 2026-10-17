#pragma once
#include "ped_smoke_state.hpp"
#include <array>

namespace grandmp::windows::ped_smoke::movement_native {
// SA facts cross-checked against the admitted Hoodlum disk image, not Compact
// layouts. Evidence, source revisions and ownership: docs/ped-smoke-movement.md.
inline constexpr std::uintptr_t allocateTask = 0x61A5A0, constructWalk = 0x668120, setTask = 0x681AF0;
inline constexpr std::uintptr_t taskPool = 0xB744A8, walkVtable = 0x86FDA8;
inline constexpr unsigned taskSize = 0x28, taskStride = 0x80, primarySlot = 3;
using Allocate = void*(__cdecl*)(unsigned);
// Six ORIGINAL arguments. The seventh tracking argument in Reversed is NOTSA.
using Construct = void*(__thiscall*)(void*, int, const Position&, float, float, bool, bool);
using SetTask = void(__thiscall*)(void*, void*, int, bool);
inline void* construct(Construct entry, void* memory, const Position& destination) {
    return entry(memory, 4 /* PEDMOVE_WALK */, destination, 0.5f, 0.f, false, true);
}
inline void transfer(SetTask entry, void* manager, void* task) { entry(manager, task, primarySlot, false); }
struct Region { std::uint32_t address, size; bool code = true; };
inline constexpr std::array regions{
    Region{0x61A500, 0xE0}, // task pool new/delete, cdecl
    Region{0x668120, 0x450}, // constructor, destructor, subtask creation
    Region{0x668570, 0xC0}, // control subtask
    Region{0x66DB80, 0xF0}, // deleting destructor / next / first subtask
    Region{0x61A3B0, 0xB0}, // complex base constructor/destructor/set child
    Region{0x6816A0, 0xA0}, Region{0x681850, 0x50}, // manager layout/flush
    Region{0x403AAB, 12}, // admitted Hoodlum manager destructor detour
    Region{0x681970, 0x30}, Region{0x681A30, 0x50}, Region{0x681AF0, 0x70},
    Region{0x421180, 0x60}, // inherited virtuals
    Region{0x607140, 0x30}, Region{0x41BE90, 0x20}, // intelligence +4, primary index 3
    Region{0x86FDA8, 44, false} // all 11 original walk virtual entries
};
} // namespace grandmp::windows::ped_smoke::movement_native
