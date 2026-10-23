#pragma once

#include "grandmp/core/remote_player_scene.hpp"

#include <cmath>
#include <cstdint>

namespace grandmp::core::pose_presentation {

// Closed model ONLY. Tokens are synthetic values, never engine addresses.
// Evidence is supplied by a simulator; this code cannot authenticate GTA state.
enum class Evidence { unknown, satisfied, contradicted };
enum class Result { accepted, blocked, unknown };
enum class Stage { admission, collision, writer, sectors, local_rw, dirty,
                   propagation, verification, terminal };
enum class Reason { incomplete, simulated_postconditions_satisfied, invalid_request,
    stale_pose, identity_changed, stale_observation, context_denied, entity_unavailable,
    lifetime_unavailable, unknown_evidence, invalid_order, collision_refused,
    invalid_observed_pose, physical_incompatible, sectors_diverged, local_rw_diverged,
    dirty_missing, propagation_failed, pose_overwritten, postconditions_failed };

struct Identity {
    std::uint64_t player = 0, generation = 0, entity = 0, owner = 0;
    bool operator==(const Identity&) const = default;
};
struct Point { float x = 0, y = 0, z = 0; };
struct Token {
    Identity identity{};
    std::uint64_t frame = 0, revision = 0;
    std::uint32_t sequence = 0;
    bool operator==(const Token&) const = default;
};
struct Expectation {
    Identity identity{};
    std::uint64_t frame = 0, lastRevision = 0;
    std::uint32_t lastSequence = 0;
};
struct Request { Token token{}; RemotePose desired{}; };
struct Snapshot {
    Token token{};
    Stage observedAt = Stage::admission;
    Evidence context = Evidence::unknown;
    Evidence liveConnectedOwned = Evidence::unknown;
    Evidence lifetime = Evidence::unknown;
};
struct Verdict {
    Result result = Result::unknown;
    Reason reason = Reason::incomplete;
    Stage at = Stage::admission;
    Token token{};
    static constexpr bool nativeSafetyProven = false;
};

// One frame, bounded state, no callbacks/providers, allocation or commands.
// Terminal outcomes latch: later observations cannot rehabilitate this frame.
class Frame final {
public:
    Frame(Expectation expected, Request request, Snapshot initial) noexcept
        : request_(request) {
        verdict_.token = request.token;
        const auto& id = expected.identity;
        if (!id.player || !id.generation || !id.entity || !id.owner || !expected.frame ||
            !request.token.revision || !finite(point(request.desired)) ||
            request.desired.id != request.token.identity.player ||
            request.desired.sequence != request.token.sequence) {
            stop(Result::blocked, Reason::invalid_request); return;
        }
        if (request.token.identity != id) {
            stop(Result::blocked, Reason::identity_changed); return;
        }
        if (request.token.frame != expected.frame ||
            request.token.sequence < expected.lastSequence ||
            request.token.revision <= expected.lastRevision) {
            stop(Result::blocked, Reason::stale_pose); return;
        }
        if (authenticate(initial)) stage_ = Stage::collision;
    }

    Stage stage() const noexcept { return stage_; }
    bool terminal() const noexcept { return stage_ == Stage::terminal; }
    Verdict verdict() const noexcept {
        auto value = verdict_;
        if (!terminal()) value.at = stage_;
        return value;
    }

    void collision(Snapshot snapshot, Evidence clear) noexcept {
        if (enter(Stage::collision, snapshot) && require(clear, Reason::collision_refused))
            stage_ = Stage::writer;
    }
    // Record the result of a simulated later physical writer. No position is
    // applied here. A differing outcome is checked again at final verification.
    void writer(Snapshot snapshot, Point actual, Evidence physicalCompatible) noexcept {
        if (!enter(Stage::writer, snapshot)) return;
        if (!finite(actual)) { stop(Result::blocked, Reason::invalid_observed_pose); return; }
        if (!require(physicalCompatible, Reason::physical_incompatible)) return;
        actual_ = actual;
        stage_ = Stage::sectors;
    }
    void sectors(Snapshot snapshot, Evidence consistent) noexcept {
        if (enter(Stage::sectors, snapshot) && require(consistent, Reason::sectors_diverged))
            stage_ = Stage::local_rw;
    }
    // Translation equality alone says nothing about the rest of the transform.
    void localRw(Snapshot snapshot, Point translation, Evidence restOfTransform) noexcept {
        if (!enter(Stage::local_rw, snapshot)) return;
        if (!finite(translation) || !same(translation, actual_)) {
            stop(Result::blocked, Reason::local_rw_diverged); return;
        }
        if (require(restOfTransform, Reason::local_rw_diverged)) stage_ = Stage::dirty;
    }
    void dirty(Snapshot snapshot, Evidence notified) noexcept {
        if (enter(Stage::dirty, snapshot) && require(notified, Reason::dirty_missing))
            stage_ = Stage::propagation;
    }
    void propagation(Snapshot snapshot, Evidence completed) noexcept {
        if (enter(Stage::propagation, snapshot) && require(completed, Reason::propagation_failed))
            stage_ = Stage::verification;
    }
    // Explicit fresh postconditions are required even after all earlier stages.
    // This cannot prove an atomic interval or detect an unreported ABA event.
    void verify(Snapshot snapshot, Point actual, Evidence physicalCompatible,
                Evidence allPostconditions) noexcept {
        if (!enter(Stage::verification, snapshot)) return;
        if (!finite(actual)) { stop(Result::blocked, Reason::invalid_observed_pose); return; }
        if (!same(actual, actual_) || !same(actual, point(request_.desired))) {
            stop(Result::blocked, Reason::pose_overwritten); return;
        }
        if (require(physicalCompatible, Reason::physical_incompatible) &&
            require(allPostconditions, Reason::postconditions_failed))
            stop(Result::accepted, Reason::simulated_postconditions_satisfied);
    }

private:
    static Point point(RemotePose pose) noexcept { return {pose.x, pose.y, pose.z}; }
    static bool finite(Point p) noexcept {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    static bool same(Point a, Point b) noexcept { return a.x == b.x && a.y == b.y && a.z == b.z; }
    void stop(Result result, Reason reason) noexcept {
        if (terminal()) return;
        verdict_.result = result; verdict_.reason = reason; verdict_.at = stage_;
        stage_ = Stage::terminal;
    }
    bool require(Evidence evidence, Reason failure) noexcept {
        if (evidence == Evidence::satisfied) return true;
        stop(evidence == Evidence::contradicted ? Result::blocked : Result::unknown,
             evidence == Evidence::contradicted ? failure : Reason::unknown_evidence);
        return false;
    }
    bool authenticate(Snapshot snapshot) noexcept {
        if (snapshot.token.identity != request_.token.identity) {
            stop(Result::blocked, Reason::identity_changed); return false;
        }
        if (snapshot.token != request_.token || snapshot.observedAt != stage_) {
            stop(Result::blocked, Reason::stale_observation); return false;
        }
        return require(snapshot.context, Reason::context_denied) &&
            require(snapshot.liveConnectedOwned, Reason::entity_unavailable) &&
            require(snapshot.lifetime, Reason::lifetime_unavailable);
    }
    bool enter(Stage expected, Snapshot snapshot) noexcept {
        if (terminal()) return false;
        if (stage_ != expected) { stop(Result::blocked, Reason::invalid_order); return false; }
        return authenticate(snapshot);
    }

    Request request_{};
    Point actual_{};
    Stage stage_ = Stage::admission;
    Verdict verdict_{};
};

} // namespace grandmp::core::pose_presentation
