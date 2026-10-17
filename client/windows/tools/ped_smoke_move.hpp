#pragma once
#include "ped_smoke_spawn.hpp"

namespace grandmp::windows::ped_smoke {
// Movement needs its own explicitly built target AND the existing spawn gate.
// Neither the older F6/F8 target nor an environment variable opens this gate.
#if defined(GRANDMP_PED_SMOKE_PHYSICAL_BUILD) && GRANDMP_PED_SMOKE_PHYSICAL_BUILD == 1 && \
    defined(GRANDMP_PED_MOVE_PHYSICAL_BUILD) && GRANDMP_PED_MOVE_PHYSICAL_BUILD == 1
inline constexpr bool movementExecutionApproved = true;
#else
inline constexpr bool movementExecutionApproved = false;
#endif
enum class MoveState { idle, assigned, moving, arrived, failed };
inline float distance2D(Position a, Position b) noexcept { return std::hypot(a.x - b.x, a.y - b.y); }

// Read-only geometry probes; no position writes, alternative routes or streaming.
template<class Probe>
const char* findMoveDestination(Probe& probe, Position origin, Position forward, Position& target) {
    if (!finite(origin) || !finite(forward)) return "position_or_forward_nonfinite";
    const float norm = std::hypot(forward.x, forward.y);
    if (!std::isfinite(norm) || norm < 0.5f || norm > 1.5f) return "forward_norm_range";
    // Sample the entire short corridor, not just its endpoint. The sphere ignores
    // only the owned PED; CJ and other entities remain collision candidates.
    float previousZ = origin.z;
    for (unsigned step = 1; step <= 8; ++step) {
        const float distance = 0.5f * step;
        target = {origin.x + distance * forward.x / norm, origin.y + distance * forward.y / norm, origin.z};
        const float queryZ = origin.z + 3.f;
        if (!finite(target) || !std::isfinite(queryZ)) return "candidate_nonfinite";
        bool found = false;
        const float ground = probe.ground(target.x, target.y, queryZ, found);
        if (!found) return "ground_not_found";
        if (!std::isfinite(ground)) return "ground_nonfinite";
        target.z = ground + 1.f;
        if (!finite(target) || std::abs(target.z - origin.z) > 1.f || std::abs(target.z - previousZ) > 0.5f)
            return "ground_step_or_height_limit";
        previousZ = target.z;
        if (probe.sphere(target)) return "corridor_sphere_hit";
    }
    return nullptr;
}

// One native task per experiment, independent of F6/F8 lifecycle states.
// After SetTask no task pointer is retained, dereferenced or deleted here.
class Movement final {
public:
    MoveState state() const noexcept { return state_; }
    template<class Engine>
    void tick(Engine& engine, const Experiment& experiment, bool gameThread, bool gameplay,
        bool request, bool remove, std::uint64_t now, float distance = 4.f) {
        if (busy_) return;
        busy_ = true;
        struct Leave { bool& busy; ~Leave() { busy = false; } } leave{busy_};
        auto fail = [&](const char* reason) { state_ = MoveState::failed; engine.moveLog("move_failed", reason); };
        auto reject = [&](const char* reason) {
            if (!rejectionLogged_) { rejectionLogged_ = true; engine.moveLog("move_rejected", reason); }
        };
        const bool active = state_ == MoveState::assigned || state_ == MoveState::moving;
        if (!gameThread) { if (active) fail("wrong_game_thread"); else if (request) reject("wrong_game_thread"); return; }
        // F8 always wins. In particular, do not query the entity after removal
        // or after Experiment has observed a cleared/recycled reference.
        if (remove || experiment.state() != State::ped_created) {
            if (active) fail(experiment.state() == State::ownership_lost ? "ownership_lost_no_retry" : "ped_lifecycle_ended_or_removal");
            if (request) reject("ped_not_created_or_terminal_or_removing");
            return;
        }
        if (!gameplay) { if (request) reject("gameplay_unavailable"); return; }
        if (request && attempted_) reject("single_move_already_consumed");
        if (state_ == MoveState::arrived || state_ == MoveState::failed) return;
        if (request && !attempted_) {
            attempted_ = true; // Consume before any native operation / nested callback.
            engine.moveLog("move_requested", "F7_one_native_walk_4m");
            if (!engine.owns(experiment.ped())) { fail("ownership_unproven"); return; }
            if (const char* reason = engine.movePosition(experiment.ped(), origin_)) { fail(reason); return; }
            if (!finite(origin_)) { fail("position_nonfinite"); return; }
            if (const char* reason = engine.moveDestination(experiment.ped(), origin_, target_)) { fail(reason); return; }
            if (!std::isfinite(distance) || distance < 1.f || distance > 4.f || !finite(target_) ||
                std::abs(distance2D(origin_, target_) - distance) > 0.05f || std::abs(target_.z - origin_.z) > 1.f) {
                fail("destination_invalid"); return;
            }
            if (const char* reason = engine.assignMove(experiment.ped(), target_)) { fail(reason); return; }
            previous_ = origin_;
            assignedAt_ = now;
            state_ = MoveState::assigned;
            engine.moveLog("move_task_assigned", "native_manager_owns_task_not_arrival");
            return;
        }
        if (!active) return;
        if (!engine.owns(experiment.ped())) { fail("ownership_lost_no_retry"); return; }
        Position position{};
        if (const char* reason = engine.movePosition(experiment.ped(), position)) { fail(reason); return; }
        if (!finite(position)) { fail("position_nonfinite"); return; }
        if (now < assignedAt_ || now - assignedAt_ > 20000) { fail("arrival_timeout_no_task_retry_use_F8"); return; }
        if (distance2D(previous_, position) > 1.5f || std::abs(previous_.z - position.z) > 1.f) {
            fail("position_discontinuity"); return;
        }
        if (state_ == MoveState::assigned && distance2D(origin_, position) >= 0.15f) {
            state_ = MoveState::moving;
            engine.moveLog("move_started", "observed_real_displacement");
        }
        const bool atDestination = state_ == MoveState::moving && distance2D(position, target_) <= 0.6f && std::abs(position.z - target_.z) <= 0.75f;
        if (!atDestination) settled_ = false;
        else if (!settled_ || distance2D(position, settledPosition_) > 0.05f) {
            settled_ = true; settledPosition_ = position; settledAt_ = now;
        }
        else if (now >= settledAt_ && now - settledAt_ >= 300) {
            state_ = MoveState::arrived;
            engine.moveLog("move_arrived", "observed_target_within_0.6m_and_stationary_300ms");
        }
        previous_ = position;
    }
private:
    MoveState state_ = MoveState::idle;
    bool attempted_ = false, rejectionLogged_ = false, busy_ = false, settled_ = false;
    Position origin_{}, target_{}, previous_{}, settledPosition_{};
    std::uint64_t assignedAt_ = 0, settledAt_ = 0;
};
} // namespace grandmp::windows::ped_smoke
