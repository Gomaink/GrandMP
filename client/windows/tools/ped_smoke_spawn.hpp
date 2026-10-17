#pragma once
#include "ped_smoke_state.hpp"

namespace grandmp::windows::ped_smoke {
inline bool finite(Position value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

// The one spawn-position attempt, shared with the disposable diagnostic tests.
// Same geometry/limits as before; no probing of alternative positions on failure.
template<class Probe>
bool findSpawnPosition(Probe& probe, Position& target) {
    auto reject = [&](const char* reason) {
        probe.trace("spawn_rejected guard=%s", reason);
        return false;
    };
    Position origin{}, right{};
    if (!probe.player(origin, right)) return reject("player_ready");
    probe.trace("cj_position x=%.9g y=%.9g z=%.9g", origin.x, origin.y, origin.z);
    probe.trace("cj_right x=%.9g y=%.9g z=%.9g", right.x, right.y, right.z);
    if (!finite(origin)) return reject("cj_position_nonfinite");
    if (!finite(right)) return reject("cj_right_nonfinite");
    const float norm = std::hypot(right.x, right.y);
    probe.trace("right_norm value=%.9g min=0.5 max=1.5", norm);
    if (!std::isfinite(norm)) return reject("right_norm_nonfinite");
    if (norm < 0.5f || norm > 1.5f) return reject("right_norm_range");
    const Position unit{right.x / norm, right.y / norm, 0.f};
    probe.trace("right_normalized x=%.9g y=%.9g z=0", unit.x, unit.y);
    // Preserve the original arithmetic order (multiply, then divide).
    target = {origin.x + 2.5f * right.x / norm, origin.y + 2.5f * right.y / norm, origin.z};
    probe.trace("candidate x=%.9g y=%.9g z=%.9g distance=2.5", target.x, target.y, target.z);
    if (!finite(target)) return reject("candidate_nonfinite");
    const float queryZ = origin.z + 3.f;
    if (!std::isfinite(queryZ)) return reject("ground_query_z_nonfinite");
    probe.trace("ground_query x=%.9g y=%.9g z=%.9g", target.x, target.y, queryZ);
    bool found = false;
    const float ground = probe.ground(target.x, target.y, queryZ, found);
    probe.trace("ground_result return_z=%.9g found=%u query_z=%.9g", ground, unsigned(found), queryZ);
    if (!found) return reject("ground_not_found");
    if (!std::isfinite(ground)) return reject("ground_return_nonfinite");
    target.z = ground + 1.f;
    const float delta = std::abs(target.z - origin.z);
    probe.trace("ground_delta spawn_z=%.9g cj_z=%.9g delta=%.9g limit=2", target.z, origin.z, delta);
    if (!finite(target) || !std::isfinite(delta)) return reject("ground_delta_nonfinite");
    if (delta > 2.f) return reject("ground_delta_limit");
    const auto hit = probe.sphere(target); // Probe logs the exact native arguments and result.
    if (hit) return reject("sphere_hit");
    probe.trace("spawn_position_accepted x=%.9g y=%.9g z=%.9g", target.x, target.y, target.z);
    return true;
}
} // namespace grandmp::windows::ped_smoke
