#pragma once

#include "grandmp/core/player_input.hpp"
#include <array>
#include <cstddef>
#include <cstring>

namespace grandmp::windows::remote_player_binding {

// Value image only, NOT a CPad pointer or a writer. Hoodlum: CPad::Update
// 541C54..541C94 copies 12 dwords New->Old; Zelda 688416/68842C reads words
// +0/+2, 688466 tests walk word +2A. No third-party struct/code is imported.
struct OnFootPadStates {
    std::array<std::byte,0x30> previous{},current{};
};
// Inverse of the authenticated unarmed walk consumer's 1/60 axis scale.
// Only walking is transmitted: non-walk locomotion is neutral, not forced run.
inline bool collectOnFootPad(const std::array<std::byte,0x30>& bytes,
                            core::PlayerControls& output) noexcept {
    output={};std::int16_t lr=0,ud=0,walk=0;
    std::memcpy(&lr,bytes.data(),2);std::memcpy(&ud,bytes.data()+2,2);
    std::memcpy(&walk,bytes.data()+0x2A,2);
    if (!walk) return true;
    const float x=lr/60.f,y=-ud/60.f,length=std::hypot(x,y);
    const float scale=length>1 ? length : 1;
    output={x/scale,y/scale,core::PlayerControls::walk};return true;
}
// Closed conversion proposal. The consumer's 688456 multiply by 1/60 supplies
// unit magnitude at raw 60; walk then caps magnitude to 1 (68847D). This does
// NOT assert a keyboard/joystick full scale of 128, camera basis, or a complete
// native context contract. Physical application remains blocked independently.
inline bool prepareOnFootPad(const core::PlayerControls& previous,
                             const core::PlayerControls& current,OnFootPadStates& output) noexcept {
    output={};
    const auto convert=[](const core::PlayerControls& controls,auto& bytes) {
        core::OnFootState checked{};checked.controls=controls;
        if (!core::validOnFoot(checked)) return false;
        const auto lr=static_cast<std::int16_t>(std::lround(controls.right*60.f));
        const auto ud=static_cast<std::int16_t>(std::lround(-controls.forward*60.f));
        const std::int16_t walk=(controls.buttons&core::PlayerControls::walk) ? 1 : 0;
        std::memcpy(bytes.data(),&lr,sizeof(lr));
        std::memcpy(bytes.data()+2,&ud,sizeof(ud));
        std::memcpy(bytes.data()+0x2A,&walk,sizeof(walk));
        return true;
    };
    if (convert(previous,output.previous) && convert(current,output.current)) return true;
    output={};return false;
}
} // namespace grandmp::windows::remote_player_binding
