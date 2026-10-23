#pragma once
#include "ped_smoke_spawn.hpp"
#include <array>
#include <cstring>

namespace grandmp::windows::remote_player_binding::second_player::factory {
// Hoodlum A559AA77...CBD26, native-player-factory.md; never an Android layout.
inline constexpr unsigned groups=8, groupStride=0x2D4;
inline constexpr std::uint32_t groupFlags=0xC098E0, groupBase=0xC09920;
inline constexpr std::uint32_t deactivate=0x609520, activate=0x609540, setPosition=0x420B80;
inline constexpr std::uint32_t baseHeight=0x536BE0, modelTable=0xA9B0C8;
inline constexpr std::uint32_t defaultTaskVtable=0x8708EC;
inline constexpr std::uint32_t taskPool=0xB744A8, taskStride=0x80;
using GroupImage=std::array<std::byte,groupStride>;
inline std::uint32_t word(const GroupImage& image,unsigned at) noexcept {
    std::uint32_t value=0;std::memcpy(&value,image.data()+at,4);return value;
}
// Native 5F7350/5E95B0 will dispose these resources. Admit only an initial,
// resource-free group, including its authenticated intelligence backpointer.
inline bool initialGroup(const GroupImage& image,std::uint32_t base) noexcept {
    for (unsigned n=0;n<8;++n) if (word(image,0xC+4*n)) return false;
    if (word(image,0x30)!=base) return false;
    for (unsigned at : {0x34u,0x38u,0x2C0u,0x2C4u}) if (word(image,at)) return false;
    for (unsigned bank=0;bank<4;++bank)
        for (unsigned n=0;n<8;++n) {
            const auto at=0x30+0xC+bank*0xA0+n*0x14;
            if (word(image,at) || word(image,at+4) || word(image,at+8)!=UINT32_MAX ||
                word(image,at+12) || image[at+16]!=std::byte{0}) return false;
        }
    return true;
}
struct CollisionSphere { ped_smoke::Position centre{};float radius=0;std::array<std::byte,4> surface{}; };
static_assert(sizeof(CollisionSphere)==0x14);
// Only sphere-only player collision data is supported, never silently omit boxes,
// triangles or line volumes. Hoodlum 418646/418715/418730/41878C.
inline bool sphereValid(const CollisionSphere& sphere) noexcept {
    return ped_smoke::finite(sphere.centre) && std::isfinite(sphere.radius) && sphere.radius>0 && sphere.radius<=2.f;
}
inline bool membersOwned(const GroupImage& image,std::uint32_t base,std::uint32_t ped) noexcept {
    if (!ped || word(image,0x30)!=base) return false;
    for (unsigned n=0;n<8;++n) {
        const auto member=word(image,0xC+4*n);
        if (member && member!=ped) return false;
    }
    return true;
}
} // namespace grandmp::windows::remote_player_binding::second_player::factory
