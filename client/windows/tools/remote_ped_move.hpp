#pragma once
#include "remote_ped.hpp"
#include "ped_smoke_move.hpp"
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace grandmp::windows::remote_ped_move {
namespace rp = remote_ped;
namespace ps = ped_smoke;
#if defined(GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD) && GRANDMP_REMOTE_PED_MOVE_PHYSICAL_BUILD == 1
inline constexpr bool physicalExecutionApproved = rp::physicalExecutionApproved;
#else
inline constexpr bool physicalExecutionApproved = false;
#endif
inline constexpr float minimumDelta = 2.f, maximumDelta = 4.f;
inline constexpr float corridorProbeRadius = 0.35f;
inline constexpr std::size_t collisionDiagnosticCapacity = 320;

enum class CollisionIdentity : std::uint8_t {
    no_hit,
    cj_hit,
    other_entity_hit,
    unclassified_hit
};

inline constexpr const char* collisionIdentityName(CollisionIdentity identity) noexcept {
    switch (identity) {
    case CollisionIdentity::no_hit: return "no_hit";
    case CollisionIdentity::cj_hit: return "cj_hit";
    case CollisionIdentity::other_entity_hit: return "other_entity_hit";
    case CollisionIdentity::unclassified_hit: return "unclassified_hit";
    }
    return "unclassified_hit";
}

// Hoodlum 0x569E20 returns the candidate CEntity address in EAX; its helper
// compares that address directly to the ignored CEntity argument. The checked
// player global holds the CPlayerPed whose CEntity base is at offset zero.
constexpr CollisionIdentity classifyCollisionIdentity(const void* hitEntity,
    const void* validatedCj, bool cjIdentityValidated, bool addressRepresentationComparable) noexcept {
    if (!hitEntity) return CollisionIdentity::no_hit;
    if (!cjIdentityValidated || !validatedCj || !addressRepresentationComparable)
        return CollisionIdentity::unclassified_hit;
    return hitEntity == validatedCj ? CollisionIdentity::cj_hit : CollisionIdentity::other_entity_hit;
}

struct SphereProbeResult {
    bool hit = false;
    CollisionIdentity identity = CollisionIdentity::no_hit;
};

struct ValidatedEntityIdentity {
    const void* address = nullptr;
    bool validated = false;
};

template<class ValidateCj>
ValidatedEntityIdentity captureCjIdentity(ValidateCj&& validateCj) {
    void* cj = nullptr;
    const bool validated = validateCj(cj);
    return {cj, validated};
}

template<class ResolveCjIdentity>
SphereProbeResult classifySphereResult(const void* hitEntity, ResolveCjIdentity&& resolveCjIdentity) {
    if (!hitEntity) return {};
    const auto cj = resolveCjIdentity();
    return {true, classifyCollisionIdentity(hitEntity, cj.address, cj.validated, true)};
}

struct CorridorCollisionDiagnostic {
    unsigned sampleIndex = 0, totalSamples = 0;
    ps::Position sample{};
    float distanceFromOrigin = 0.f, radius = corridorProbeRadius;
    bool nativeHit = false;
    CollisionIdentity hitIdentity = CollisionIdentity::no_hit;
};

inline bool formatCollisionDiagnostic(char* output, std::size_t capacity, std::uint64_t generation,
    std::uint32_t sequence, const CorridorCollisionDiagnostic& diagnostic) noexcept {
    if (!output || capacity == 0) return false;
    const int count = std::snprintf(output, capacity,
        "attempt=1 generation=%llu sequence=%u sample=%u/%u xyz=%.6e,%.6e,%.6e distance=%.6e radius=%.6e native_return=%s hit_identity=%s excluded=owned_ped flags=111110",
        static_cast<unsigned long long>(generation), sequence, diagnostic.sampleIndex, diagnostic.totalSamples,
        diagnostic.sample.x, diagnostic.sample.y, diagnostic.sample.z, diagnostic.distanceFromOrigin,
        diagnostic.radius, diagnostic.nativeHit ? "non_null" : "null", collisionIdentityName(diagnostic.hitIdentity));
    return count >= 0 && static_cast<std::size_t>(count) < capacity;
}

template<class Probe>
const char* findDestination(Probe& probe, ps::Position origin, ps::Position candidate, ps::Position& target,
    CorridorCollisionDiagnostic* collision = nullptr) {
    if (collision) *collision = {};
    if (!ps::finite(origin) || !ps::finite(candidate)) return "candidate_nonfinite";
    const float distance = ps::distance2D(origin, candidate);
    if (distance < 1.f || distance > maximumDelta || std::abs(candidate.z-origin.z)>1.f) return "local_distance_or_height_limit";
    const unsigned steps = static_cast<unsigned>(std::ceil(distance/0.5f));
    ps::Position previous = origin;
    float accumulatedDistance = 0.f;
    for (unsigned step=1;step<=steps;++step) {
        const float t=static_cast<float>(step)/steps;
        target={origin.x+(candidate.x-origin.x)*t,origin.y+(candidate.y-origin.y)*t,origin.z};
        bool found=false;
        const float ground=probe.ground(target.x,target.y,origin.z+3.f,found);
        if (!found || !std::isfinite(ground)) return "ground_missing_or_nonfinite";
        target.z=ground+1.f;
        if (!ps::finite(target) || std::abs(target.z-origin.z)>1.f || std::abs(target.z-previous.z)>0.5f) return "ground_step_or_height_limit";
        accumulatedDistance += std::hypot(ps::distance2D(previous,target),target.z-previous.z);
        const SphereProbeResult sphere = probe.sphere(target);
        if (sphere.hit) {
            if (collision) *collision = {step, steps, target, accumulatedDistance, corridorProbeRadius, true, sphere.identity};
            return "corridor_sphere_hit";
        }
        previous=target;
    }
    return nullptr;
}

// A single task attempt for this ASI lifetime, even across reconnects. Network
// owns only IntentSink; this wrapper is exclusively the admitted phase consumer.
class Controller final {
public:
    ps::State state() const noexcept { return lifecycle_.state(); }
    std::uintptr_t ped() const noexcept { return lifecycle_.ped(); }
    rp::Key key() const noexcept { return lifecycle_.key(); }
    bool poisoned() const noexcept { return lifecycle_.poisoned(); }
    ps::MoveState moveState() const noexcept { return movement_.state(); }
    void disable() noexcept { lifecycle_.disable(); }
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
    template<class Engine, class Diagnostic>
    void tick(Engine& engine, const rp::Intent& incoming, bool nativeThread, bool gameplay, std::uint64_t now, Diagnostic& diagnostic) {
#else
    template<class Engine>
    void tick(Engine& engine, const rp::Intent& incoming, bool nativeThread, bool gameplay, std::uint64_t now) {
#endif
        if (!nativeThread || !gameplay || busy_ || poisoned()) return;
        busy_=true;
        struct Leave { bool& busy; ~Leave() { busy=false; } } leave{busy_};
        if (incoming.key.generation >= desired_.key.generation) {
            if (incoming.revision>desired_.revision) desired_=incoming;
            else if (incoming.revision==desired_.revision && incoming.key==desired_.key &&
                incoming.sampleRevision>desired_.sampleRevision && incoming.pose.sequence>=desired_.pose.sequence) {
                desired_.pose=incoming.pose; desired_.sampleRevision=incoming.sampleRevision;
            }
        }
        const bool departing=!desired_.present || desired_.blocked || (key().player && desired_.key!=key());
        lifecycle_.tick(engine,desired_,true,true,now); // Leave/ownership always precedes task work.
        if (key()!=anchorKey_) { anchorKey_=key(); anchored_=false; ended_=false; smallLogged_=false; }
        if (departing || state()!=ps::State::ped_created) {
            if (anchored_ && !ended_) {
                engine.moveLog(departing ? "remote_move_cancelled" : "remote_move_failed",
                    departing ? "leave_or_session_change" : "ped_lifecycle_or_ownership_ended");
                ended_=true;
            }
            return; // Never read PED pose after removal/ownership failure.
        }
        if (attempted_ || ended_) {
            if (!ended_ && anchored_) {
                movement_.tick(engine,lifecycle_.entity(),true,true,false,false,now,distance_);
                ended_=movement_.state()==ps::MoveState::arrived || movement_.state()==ps::MoveState::failed;
            }
            return;
        }
        if (!anchored_) {
            const auto& first=desired_.initialPose;
            ps::Position actual{};
            if (!first.id || first.id!=key().player || !first.sequence ||
                !protocol::validCoordinates(first.x,first.y,first.z) || !engine.initialPosition(localAnchor_) ||
                engine.movePosition(ped(),actual) || !ps::finite(localAnchor_) ||
                ps::distance2D(localAnchor_,actual)>0.25f || std::abs(localAnchor_.z-actual.z)>0.25f) {
                attempted_=true; ended_=true; engine.moveLog("remote_move_failed","anchor_identity_or_local_pose_invalid"); return;
            }
            remoteAnchor_=first; anchored_=true;
            engine.recordRemoteSample(first,desired_.sampleRevision);
            engine.moveLog("remote_move_position_received","first_validated_pose");
            engine.moveLog("remote_move_anchor_set","first_remote_pose_and_safe_local_spawn");
        }
        const auto& pose=desired_.pose;
        if (pose.id!=key().player || pose.sequence<=remoteAnchor_.sequence) return;
        const ps::Position delta{pose.x-remoteAnchor_.x,pose.y-remoteAnchor_.y,pose.z-remoteAnchor_.z};
        const float length=std::hypot(delta.x,delta.y);
        if (ps::finite(delta) && length<minimumDelta && std::abs(delta.z)<=1.f) {
            if (!smallLogged_) { smallLogged_=true; engine.moveLog("remote_move_rejected","delta_below_2m_waiting"); }
            return;
        }
        attempted_=true; // One attempt BEFORE any engine query/allocation/reentry.
        engine.recordRemoteSample(pose,desired_.sampleRevision);
        engine.moveLog("remote_move_position_received","changed_pose_consumed_once");
        if (!ps::finite(delta) || !std::isfinite(length) || length>maximumDelta || std::abs(delta.z)>1.f) {
            ended_=true; engine.moveLog("remote_move_rejected","remote_delta_nonfinite_distance_or_height_limit"); return;
        }
        ps::Position actual{};
        if (engine.movePosition(ped(),actual) || ps::distance2D(actual,localAnchor_)>0.25f || std::abs(actual.z-localAnchor_.z)>0.25f) {
            ended_=true; engine.moveLog("remote_move_failed","local_anchor_drift_or_identity"); return;
        }
        const ps::Position candidate{localAnchor_.x+delta.x,localAnchor_.y+delta.y,localAnchor_.z};
        distance_=ps::distance2D(actual,candidate);
        if (!ps::finite(candidate) || distance_<1.f || distance_>maximumDelta) {
            ended_=true; engine.moveLog("remote_move_rejected","local_target_distance_limit"); return;
        }
        engine.prepareRemoteMove(candidate);
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
        diagnostic.beforeAttempt(*this); // Metadata before ORIGINAL one-shot Movement tick; never authorizes it.
#endif
        movement_.tick(engine,lifecycle_.entity(),true,true,true,false,now,distance_);
        ended_=movement_.state()==ps::MoveState::failed;
    }
private:
    rp::Controller lifecycle_;
    rp::Intent desired_{};
    ps::Movement movement_;
    rp::Key anchorKey_{};
    core::RemotePose remoteAnchor_{};
    ps::Position localAnchor_{};
    float distance_=0;
    bool anchored_=false,attempted_=false,ended_=false,smallLogged_=false,busy_=false;
};
} // namespace grandmp::windows::remote_ped_move
