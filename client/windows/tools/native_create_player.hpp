#pragma once

#include "ped_smoke_spawn.hpp"
#include <array>
#include <bit>
#include <cstdint>

namespace grandmp::windows::remote_player_binding::second_player::create_player {
namespace ps=ped_smoke;

// Hoodlum command/data ABI only; NOT an executor or a CRunningScript replica.
// Evidence and unresolved execution obligations: docs/native-create-player.md.
inline constexpr std::uint32_t startNewScript=0x464C20, processOneCommand=0x469EB0;
inline constexpr std::uint16_t createOpcode=0x0053, terminateOpcode=0x004E;
inline constexpr std::size_t terminateOffset=25;
using Program=std::array<std::uint8_t,27>;

// CREATE_PLAYER 1, X, Y, groundZ -> local[0]; TERMINATE_THIS_SCRIPT.
// Output local[0] is PLAYER INDEX 1, not a PED pointer or pool handle.
// The engine adds its model's centre-to-base distance to groundZ itself.
// This buffer must NOT be submitted until context/lifetime/effects are proven.
inline bool encodeGround(ps::Position ground,Program& output) noexcept {
    output={};
    // <= -100 selects another native ground query (constant at 0x859014).
    if (!ps::finite(ground) || ground.z<=-100.f) return false;
    Program code{};
    std::size_t n=0;
    auto word=[&](std::uint16_t v) {
        code[n++]=static_cast<std::uint8_t>(v);
        code[n++]=static_cast<std::uint8_t>(v>>8);
    };
    auto value=[&](std::uint8_t type,std::uint32_t v) {
        code[n++]=type;
        for (unsigned shift=0;shift<32;shift+=8) code[n++]=static_cast<std::uint8_t>(v>>shift);
    };
    word(createOpcode);
    value(1,1); // immediate int32: fixed native player slot, never slot 0.
    value(6,std::bit_cast<std::uint32_t>(ground.x));
    value(6,std::bit_cast<std::uint32_t>(ground.y));
    value(6,std::bit_cast<std::uint32_t>(ground.z));
    code[n++]=3;word(0); // native StoreParameters local-variable destination.
    word(terminateOpcode);
    output=code;
    return true;
}

// Borrow the existing single-position probe unchanged. Capture the actual
// ground return, rather than subtracting 1 from the rounded NPC centre Z.
// The .35 sphere is NOT proof that ClearSpaceForMissionEntity is harmless.
namespace detail {
template<class Probe>
struct GroundProbe {
    Probe& source;
    float groundZ=0.f;
    bool player(ps::Position& p,ps::Position& right) { return source.player(p,right); }
    float ground(float x,float y,float z,bool& found) {
        groundZ=source.ground(x,y,z,found);return groundZ;
    }
    auto sphere(ps::Position p) { return source.sphere(p); }
    template<class... Args> void trace(const char* format,Args... args) {
        source.trace(format,args...);
    }
};
} // namespace detail
template<class Probe>
bool prepare(Probe& probe,Program& output) {
    output={};
    detail::GroundProbe<Probe> captured{probe};
    ps::Position centre{};
    if (!ps::findSpawnPosition(captured,centre)) return false;
    return encodeGround({centre.x,centre.y,captured.groundZ},output);
}
} // namespace grandmp::windows::remote_player_binding::second_player::create_player
