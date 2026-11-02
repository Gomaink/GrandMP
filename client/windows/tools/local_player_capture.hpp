#pragma once
#include "grandmp/windows/native_player_input.hpp"
#include "grandmp/core/local_player_snapshot.hpp"
#include "ped_smoke_spawn.hpp"

namespace grandmp::windows::remote_player_binding {
// All state below belongs to the native producer. Velocity is a measured
// position delta / monotonic seconds, NOT an unproven GTA timestep conversion.
class LocalKinematics final {
public:
    bool sample(ped_smoke::Position position,ped_smoke::Position forward,float view,
                std::uint64_t now,core::OnFootState& result) noexcept {
        result={};
        if (!ped_smoke::finite(position) || !ped_smoke::finite(forward) || !std::isfinite(view) ||
            std::hypot(forward.x,forward.y)<0.5f) { reset();return false; }
        const auto before=previous_;const auto then=last_;
        previous_=position;last_=now;
        if (!warm_ || now<=then || now-then>500) { warm_=true;return false; }
        const float dt=static_cast<float>(now-then)/1000.f;
        result.x=position.x;result.y=position.y;result.z=position.z;
        result.vx=(position.x-before.x)/dt;result.vy=(position.y-before.y)/dt;result.vz=(position.z-before.z)/dt;
        result.heading=std::atan2(-forward.x,forward.y);
        result.viewHeading=std::remainder(view,6.283185307f);
        return core::validOnFoot(result);
    }
    void reset() noexcept { warm_=false; }
private:
    ped_smoke::Position previous_{};
    std::uint64_t last_=0;
    bool warm_=false;
};
} // namespace grandmp::windows::remote_player_binding
