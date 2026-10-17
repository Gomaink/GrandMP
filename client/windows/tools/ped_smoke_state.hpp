#pragma once

#include <cmath>
#include <cstdint>

namespace grandmp::windows::ped_smoke {
// Default artifact stays closed. Only the separate, explicitly requested smoke
// target defines this build gate; no environment variable/export overrides it.
#if defined(GRANDMP_PED_SMOKE_PHYSICAL_BUILD) && GRANDMP_PED_SMOKE_PHYSICAL_BUILD == 1
inline constexpr bool physicalExecutionApproved = true;
#else
inline constexpr bool physicalExecutionApproved = false;
#endif
inline constexpr unsigned male01 = 7;
struct Position { float x, y, z; };
static_assert(sizeof(Position) == 12 && alignof(Position) == 4);
enum class State { waiting, model_requested, model_loaded, ped_created, cleanup_pending, removed, failed, ownership_lost };
inline const char* name(State state) noexcept {
    switch (state) {
    case State::waiting: return "waiting";
    case State::model_requested: return "model_requested";
    case State::model_loaded: return "model_loaded";
    case State::ped_created: return "ped_created";
    case State::cleanup_pending: return "cleanup_pending";
    case State::removed: return "removed";
    case State::ownership_lost: return "ownership_lost";
    default: return "failed";
    }
}

class PressEdge final {
public:
    bool sample(bool down, bool eligible) noexcept {
        const bool pressed = down && !down_;
        down_ = down; // Observe releases/background holds without synthesizing a press on refocus.
        return eligible && pressed;
    }
private:
    bool down_ = false;
};

// Local one-shot controller only; no game layouts, network or shared-core API.
// Native operations are supplied by the Win32 adapter or a disposable fake.
class Experiment final {
public:
    State state() const noexcept { return state_; }
    std::uintptr_t ped() const noexcept { return ped_; }
    bool terminal() const noexcept { return state_ == State::removed || state_ == State::failed || state_ == State::ownership_lost; }
    void fail() noexcept { if (!terminal()) state_ = State::failed; }

    template<class Engine>
    void tick(Engine& engine, bool gameThread, bool gameplay, bool remove, std::uint64_t now) {
        if (busy_ || terminal()) return; // F8/exports cannot retry or reacquire a terminal entity.
        if (!gameThread) { change(engine, State::failed, "wrong_game_thread"); return; }
        busy_ = true;
        struct Leave { bool& busy; ~Leave() { busy = false; } } leave{busy_};
        if (!gameplay) return;
        if (remove || state_ == State::cleanup_pending) {
            if (ped_ && !engine.owns(ped_)) {
                change(engine, State::ownership_lost, "ownership_lost_removal_cancelled_no_retry");
                return;
            }
            if (ped_ && !engine.remove(ped_)) {
                change(engine, State::failed, "removal_refused_no_retry");
                return;
            }
            ped_ = 0;
            change(engine, State::removed, remove ? "explicit_removal_no_respawn" : "invalid_render_owned_ped_removed_no_respawn");
            return;
        }
        if (state_ == State::waiting) {
            requestedAt_ = now;
            change(engine, State::model_requested, "request_male01_once");
            if (!engine.requestModel()) change(engine, State::failed, "streaming_request_failed");
        } else if (state_ == State::model_requested) {
            if (now < requestedAt_ || now - requestedAt_ > 10000) {
                change(engine, State::failed, "streaming_timeout");
            } else if (engine.modelLoaded()) {
                change(engine, State::model_loaded, "streaming_state_loaded");
            }
        } else if (state_ == State::model_loaded) {
            Position position{};
            if (!engine.revalidateModel()) {
                change(engine, State::failed, "spawn_model_revalidation_failed");
                return;
            }
            if (!engine.spawnPosition(position)) {
                change(engine, State::failed, "spawn_position_rejected_see_spawn_probe");
                return;
            }
            if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z)) {
                change(engine, State::failed, "spawn_position_nonfinite");
                return;
            }
            // Consume the one spawn attempt BEFORE entering the engine. A
            // nested callback or a null/invalid result never authorizes retry.
            state_ = State::failed;
            ped_ = engine.create(position);
            if (!ped_) {
                change(engine, State::failed, "addped_returned_null_no_retry");
                return;
            }
            if (!engine.owns(ped_)) {
                change(engine, State::ownership_lost, "addped_returned_identity_unknown_retained_no_delete_no_retry");
                return;
            }
            if (!engine.visible(ped_)) {
                change(engine, State::cleanup_pending, "addped_owned_render_invalid_cleanup_next_safe_phase");
                return;
            }
            change(engine, State::ped_created, "one_owned_ped_with_render_object");
        } else if (state_ == State::ped_created && !engine.owns(ped_)) {
            change(engine, State::ownership_lost, "owned_ped_lost_no_respawn");
        }
    }
private:
    template<class Engine> void change(Engine& engine, State state, const char* reason) {
        state_ = state;
        engine.log(state, reason);
    }
    State state_ = State::waiting;
    std::uintptr_t ped_ = 0;
    std::uint64_t requestedAt_ = 0;
    bool busy_ = false;
};
} // namespace grandmp::windows::ped_smoke
