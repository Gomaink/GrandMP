#pragma once

#include "grandmp/core/remote_player_scene.hpp"

#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace grandmp::core {

struct MovementSession {
    std::uint64_t generation = 0, player = 0;
    bool operator==(const MovementSession&) const = default;
};

struct MovementPoint { float x = 0, y = 0, z = 0; };

// Value-only scheduling on the admitted consumer thread. No network callbacks,
// engine pointers, task allocation, cancellation or task destruction live here.
// Slice 1 has no native adapter and is not connected to any physical ASI gate.
class ContinuousRemoteMovement final {
public:
    enum class State { inactive, ready, issuing, assigned, moving, awaiting_release, cancelled, failed };
    enum class Slots { empty, busy, unknown };
    // Supplied by an observer of real displacement/settling, NOT by SetTask.
    enum class Progress { assigned, moving, arrived, failed };
    enum class SampleResult { accepted, duplicate, stale, invalid, inactive };
    enum class Failure { none, invalid_target, backend_refused, observation_failed,
        arrival_without_start, task_state_unknown, task_timeout, slot_timeout,
        clock_regressed, identity_lost, attempt_exhausted };
    struct Command {
        MovementSession session{};
        std::uint64_t attempt = 0, sampleRevision = 0;
        RemotePose sample{};
        MovementPoint origin{}, destination{};
    };

    // Existing Alpha 0.7 leg/observation bounds. No clamping or teleporting.
    static constexpr float minimumLeg = 2.f, maximumLeg = 4.f;
    static constexpr std::uint64_t timeoutMs = 20000, minimumCommandIntervalMs = 300;

    State state() const noexcept { return state_; }
    Failure failure() const noexcept { return failure_; }
    MovementSession session() const noexcept { return session_; }
    bool poisoned() const noexcept { return poisoned_; }
    unsigned pendingCount() const noexcept { return pending_ ? 1u : 0u; }
    const Command& command() const noexcept { return command_; }

    // The adapter must first prove native ownership and a safe local spawn.
    // Re-admission requires explicit confirmation that the previous entity is
    // absent, never just an observed arrival or a new generation in a packet.
    bool begin(MovementSession session, RemotePose first, MovementPoint spawn,
               std::uint64_t now) noexcept {
        if (poisoned_ || state_ != State::inactive || !session.player ||
            !session.generation || session.generation <= consumedGeneration_ ||
            first.id != session.player || !valid(first) || !finite(spawn)) return false;
        session_ = session;
        consumedGeneration_ = session.generation;
        remoteAnchor_ = latest_ = first;
        localAnchor_ = spawn;
        sampleRevision_ = 0;
        lastTime_ = now;
        issuedInSession_ = waiting_ = false;
        failure_ = Failure::none;
        state_ = State::ready;
        return true;
    }

    // Called with the mutex-protected IntentSink snapshot, on the consumer.
    // Roster rejects duplicate UDP sequences. Scene interpolation legitimately
    // changes XYZ within a sequence, and is ordered by sampleRevision instead.
    SampleResult offer(MovementSession session, RemotePose pose,
                       std::uint64_t revision) noexcept {
        if (session != session_ || terminal()) return SampleResult::inactive;
        if (!revision || pose.id != session_.player || !valid(pose)) return SampleResult::invalid;
        if (pose.sequence < latest_.sequence || revision < sampleRevision_) return SampleResult::stale;
        if (revision == sampleRevision_) return pose.sequence == latest_.sequence && samePosition(pose, latest_)
            ? SampleResult::duplicate : SampleResult::stale;
        const bool unchanged = samePosition(pose, latest_);
        latest_ = pose;
        sampleRevision_ = revision;
        if (unchanged) {
            if (pending_) pending_ = Sample{pose, revision};
            return SampleResult::duplicate;
        }
        pending_ = Sample{pose, revision}; // Exactly one latest value, including while active.
        return SampleResult::accepted;
    }

    // The adapter checks actual thread/phase before ALL engine operations. This
    // guard also prevents command emission outside that admitted context.
    std::optional<Command> next(MovementPoint actual, Slots slots,
        bool nativeThread, bool gameplay, std::uint64_t now) noexcept {
        if (!nativeThread || !gameplay || terminal() || !time(now)) return std::nullopt;
        if (active()) {
            if (now - commandAt_ > timeoutMs) fail(Failure::task_timeout);
            return std::nullopt;
        }
        if (state_ == State::awaiting_release || pending_) {
            if (slots == Slots::unknown) { fail(Failure::task_state_unknown); return std::nullopt; }
            if (waiting_ && now - waitingAt_ > timeoutMs) { fail(Failure::slot_timeout); return std::nullopt; }
            if (slots == Slots::busy) {
                if (!waiting_) { waiting_ = true; waitingAt_ = now; }
                return std::nullopt;
            }
            waiting_ = false;
            state_ = State::ready; // Arrived AND all protected slots proven empty.
        }
        if (!pending_) return std::nullopt;
        if (issuedInSession_ && now - commandAt_ < minimumCommandIntervalMs) return std::nullopt;
        const auto sample = *pending_;
        pending_.reset(); // Consume before any backend operation or reentry.
        const MovementPoint destination{localAnchor_.x + (sample.pose.x - remoteAnchor_.x),
            localAnchor_.y + (sample.pose.y - remoteAnchor_.y), actual.z};
        const float distance = std::hypot(destination.x - actual.x, destination.y - actual.y);
        if (!finite(actual) || !finite(destination) || !std::isfinite(distance) ||
            std::abs(sample.pose.z - remoteAnchor_.z) > 1.f || distance > maximumLeg) {
            fail(Failure::invalid_target); return std::nullopt;
        }
        if (distance < minimumLeg) return std::nullopt; // Deadband; retain sequence watermarks.
        if (attempt_ == std::numeric_limits<std::uint64_t>::max()) {
            fail(Failure::attempt_exhausted); return std::nullopt;
        }
        command_ = {session_, ++attempt_, sample.revision, sample.pose, actual, destination};
        commandAt_ = now;
        issuedInSession_ = true;
        state_ = State::issuing;
        return command_;
    }

    // Backend must revalidate ownership, geometry/collision and task slots for
    // every command. Any refusal is terminal for movement in this generation.
    bool acknowledge(std::uint64_t attempt, bool accepted, std::uint64_t now) noexcept {
        if (state_ != State::issuing || attempt != command_.attempt || !time(now)) return false;
        if (now - commandAt_ > timeoutMs) { fail(Failure::task_timeout); return false; }
        if (!accepted) { fail(Failure::backend_refused); return false; }
        state_ = State::assigned;
        return true;
    }

    void observe(std::uint64_t attempt, Progress progress, std::uint64_t now) noexcept {
        if ((state_ != State::assigned && state_ != State::moving) ||
            attempt != command_.attempt || !time(now)) return;
        if (now - commandAt_ > timeoutMs) { fail(Failure::task_timeout); return; }
        if (progress == Progress::failed) { fail(Failure::observation_failed); return; }
        if (progress == Progress::moving) state_ = State::moving;
        else if (progress == Progress::arrived) {
            if (state_ != State::moving) { fail(Failure::arrival_without_start); return; }
            state_ = State::awaiting_release;
            waiting_ = true;
            waitingAt_ = now;
        }
    }

    // These transitions never touch engine/task memory. Leave wins over pending
    // work; the existing entity lifecycle owns removal and active-task cleanup.
    void cancel(MovementSession session) noexcept {
        if (session != session_ || state_ == State::inactive || poisoned_) return;
        pending_.reset();
        state_ = State::cancelled;
    }
    bool confirmRemoved(MovementSession session) noexcept {
        if (poisoned_ || session != session_ ||
            (state_ != State::cancelled && state_ != State::failed)) return false;
        pending_.reset();
        command_ = {};
        session_ = {};
        state_ = State::inactive;
        return true;
    }
    void invalidateIdentity() noexcept {
        poisoned_ = true;
        fail(Failure::identity_lost); // No new generation can override unknown native identity.
    }

private:
    struct Sample { RemotePose pose; std::uint64_t revision; };
    static bool finite(MovementPoint p) noexcept {
        return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z);
    }
    static bool valid(RemotePose p) noexcept {
        return p.id && p.sequence && protocol::validCoordinates(p.x, p.y, p.z);
    }
    static bool samePosition(RemotePose a, RemotePose b) noexcept {
        return a.x == b.x && a.y == b.y && a.z == b.z;
    }
    bool terminal() const noexcept {
        return state_ == State::inactive || state_ == State::cancelled || state_ == State::failed;
    }
    bool active() const noexcept {
        return state_ == State::issuing || state_ == State::assigned || state_ == State::moving;
    }
    bool time(std::uint64_t now) noexcept {
        if (now < lastTime_) { fail(Failure::clock_regressed); return false; }
        lastTime_ = now;
        return true;
    }
    void fail(Failure reason) noexcept {
        failure_ = reason;
        pending_.reset();
        state_ = State::failed;
    }
    State state_ = State::inactive;
    Failure failure_ = Failure::none;
    MovementSession session_{};
    RemotePose remoteAnchor_{}, latest_{};
    MovementPoint localAnchor_{};
    std::optional<Sample> pending_;
    Command command_{};
    std::uint64_t consumedGeneration_ = 0, sampleRevision_ = 0, attempt_ = 0;
    std::uint64_t commandAt_ = 0, lastTime_ = 0, waitingAt_ = 0;
    bool poisoned_ = false, issuedInSession_ = false, waiting_ = false;
};

} // namespace grandmp::core
