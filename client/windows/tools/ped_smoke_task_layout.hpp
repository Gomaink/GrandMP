#pragma once
#include <cstdint>

namespace grandmp::windows::ped_smoke::task_layout {
// Hoodlum A559AA77...CBD26: docs/ped-smoke-movement-followup1.md.
// 607161/607168: embedded manager and intelligence owner; 6816A6: manager owner.
// 41BE99/681B1C: primary index 3. These are SA PC fields, never Android layouts.
inline constexpr std::uint32_t intelligence = 0x47C, manager = 4, managerOwner = 0x2C;
inline constexpr unsigned primarySlot = 3;
inline constexpr std::uint32_t pointerBytes = 4;
} // namespace grandmp::windows::ped_smoke::task_layout
