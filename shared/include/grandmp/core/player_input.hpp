#pragma once

#include <cmath>
#include <cstdint>
#include <chrono>

namespace grandmp::core {

inline constexpr auto onFootInputTimeout = std::chrono::milliseconds(500);

// Portable on-foot input, NOT a CPad/CControllerState layout. X right, Y forward
// in the viewHeading basis. Radians [-pi,pi]; velocity in world units/second.
// No combat/vehicle buttons are admitted in this first increment.
struct PlayerControls {
    float right = 0, forward = 0;
    std::uint16_t buttons = 0;
    static constexpr std::uint16_t walk = 1;
    bool operator==(const PlayerControls&) const = default;
};
struct OnFootState {
    float x = 0, y = 0, z = 0;
    float vx = 0, vy = 0, vz = 0;
    float heading = 0, viewHeading = 0;
    PlayerControls controls{};
    bool operator==(const OnFootState&) const = default;
};
inline bool validOnFoot(const OnFootState& s) noexcept {
    const auto bounded=[](float v,float limit) { return std::isfinite(v) && std::abs(v)<=limit; };
    return bounded(s.x,100000.f) && bounded(s.y,100000.f) && bounded(s.z,100000.f) &&
        bounded(s.vx,100.f) && bounded(s.vy,100.f) && bounded(s.vz,100.f) &&
        bounded(s.heading,3.1415927f) && bounded(s.viewHeading,3.1415927f) &&
        bounded(s.controls.right,1.f) && bounded(s.controls.forward,1.f) &&
        (s.controls.buttons & ~PlayerControls::walk)==0;
}

} // namespace grandmp::core
