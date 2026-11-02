#pragma once
#include "grandmp/core/remote_player_controller.hpp"

namespace grandmp::windows::independent_control_proof {
#if defined(GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD) && GRANDMP_INDEPENDENT_NATIVE_CONTROL_PROOF_BUILD == 1
inline constexpr bool enabled=true;
#else
inline constexpr bool enabled=false;
#endif
enum class Phase { walking, turning, stopped };
inline const char* name(Phase phase) noexcept {
    return phase==Phase::walking ? "walking" : phase==Phase::turning ? "turning" : "stopped";
}
// Local source, NOT a UDP sender or a movement simulator. Static XYZ is only the
// factory spawn/reference; this proof has NO presentation consumer. The real
// Controller and Binding still perform all freshness/identity checks.
class Source final {
public:
    using Time=core::RemoteRoster::Time;
    static constexpr std::uint64_t recipient=0x1100,remote=0x1101;
    bool begin(core::OnFootState reference,Time now,core::RemotePlayerController& controller) {
        if(used_)return false;
        used_=true;
        if(!core::validOnFoot(reference))return false;
        reference_=reference;reference_.vx=reference_.vy=reference_.vz=0;
        start_=last_=now;controller.beginSession(recipient);active_=true;
        return publish(now,controller);
    }
    bool publish(Time now,core::RemotePlayerController& controller) {
        if(!active_ || now<last_ || sequence_==UINT32_MAX){active_=false;return false;}
        last_=now;
        using namespace std::chrono_literals;
        phase_=now-start_<2s ? Phase::walking : now-start_<4s ? Phase::turning : Phase::stopped;
        auto sample=reference_;
        sample.controls=phase_==Phase::walking ? core::PlayerControls{0,1,core::PlayerControls::walk} :
            phase_==Phase::turning ? core::PlayerControls{1,0,core::PlayerControls::walk} : core::PlayerControls{};
        ++sequence_;
        (void)roster_.position({remote,sequence_,sample.x,sample.y,sample.z},now);
        if(!controller.receive({recipient,remote,sequence_,sample},roster_,now)){active_=false;return false;}
        controller.synchronize(roster_,now);return true;
    }
    Phase phase() const noexcept {return phase_;}
    bool complete(Time now) const noexcept {return active_ && now>=start_+std::chrono::seconds(6);}
private:
    core::RemoteRoster roster_;
    core::OnFootState reference_{};
    Time start_{},last_{};
    std::uint32_t sequence_=0;
    Phase phase_=Phase::walking;
    bool used_=false,active_=false;
};
}
