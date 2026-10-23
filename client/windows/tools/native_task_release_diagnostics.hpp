#pragma once
#include "continuous_remote_native_observer.hpp"
#include <cstdio>

namespace grandmp::windows::task_release_diagnostics {
namespace native = continuous_remote_native;
using Slots = native::Coordinator::Slots;
// Supplied by the ORIGINAL one-walk observer, not inferred from task slots.
enum class Walk { before_assignment, assigned, started, arrived };
enum class Reason { initial, transition, empty_after_arrival, callback_limit,
    authentication_failed, session_changed, attempt_changed, callback_regressed,
    walk_regressed, invalid_configuration };
struct Event {
    std::uint64_t generation = 0, attempt = 0;
    unsigned elapsedCallbacks = 0, limit = 0, occupiedMask = 0;
    Slots slots = Slots::unknown;
    Walk walk = Walk::before_assignment;
    Reason reason = Reason::initial;
    native::Reason authentication = native::Reason::none;
    bool sampled = false, afterArrival = false, terminal = false;
};
inline const char* name(Slots value) noexcept {
    switch (value) { case Slots::empty: return "empty"; case Slots::busy: return "busy"; default: return "unknown"; }
}
inline const char* name(Walk value) noexcept {
    switch (value) {
    case Walk::before_assignment: return "before_assignment";
    case Walk::assigned: return "assigned";
    case Walk::started: return "started";
    default: return "arrived";
    }
}
inline const char* name(Reason value) noexcept {
    switch (value) {
    case Reason::initial: return "initial";
    case Reason::transition: return "transition";
    case Reason::empty_after_arrival: return "empty_after_arrival";
    case Reason::callback_limit: return "callback_limit";
    case Reason::authentication_failed: return "authentication_failed";
    case Reason::session_changed: return "session_changed";
    case Reason::attempt_changed: return "attempt_changed";
    case Reason::callback_regressed: return "callback_regressed";
    case Reason::walk_regressed: return "walk_regressed";
    default: return "invalid_configuration";
    }
}
inline const char* name(native::Reason value) noexcept {
    switch (value) {
    case native::Reason::none: return "none";
    case native::Reason::context: return "context";
    case native::Reason::identity: return "identity";
    case native::Reason::engine: return "engine";
    case native::Reason::ownership: return "ownership";
    case native::Reason::intelligence: return "intelligence";
    case native::Reason::manager_owner: return "manager_owner";
    case native::Reason::incomplete_read: return "incomplete_read";
    case native::Reason::changed: return "changed";
    case native::Reason::pool: return "pool";
    default: return "removal_not_completed";
    }
}
inline constexpr std::size_t messageCapacity = 320;
inline bool format(char* output, std::size_t capacity, const Event& event) noexcept {
    if (!output || !capacity) return false;
    char mask[4]{};
    if (event.slots != Slots::unknown && event.sampled)
        std::snprintf(mask, sizeof(mask), "%u", event.occupiedMask & 15u);
    const int count = std::snprintf(output, capacity,
        "generation=%llu attempt=%llu elapsed_callbacks=%u limit=%u slots=%s mask=%s walk=%s phase=%s reason=%s auth=%s sampled=%u terminal=%u",
        static_cast<unsigned long long>(event.generation), static_cast<unsigned long long>(event.attempt),
        event.elapsedCallbacks, event.limit, name(event.slots), mask[0] ? mask : "unknown", name(event.walk),
        event.afterArrival ? "post_arrival" : "pre_arrival", name(event.reason), name(event.authentication),
        unsigned(event.sampled), unsigned(event.terminal));
    if (count < 0 || static_cast<std::size_t>(count) >= capacity) { output[0] = '\0'; return false; }
    return true;
}

// Closed instrumentation only: no ASI includes this class. It retains only values,
// NEVER Entity, PED/manager/task addresses or a backend/sink for later use.
class Observer final {
public:
    static constexpr unsigned defaultLimit = 600, maximumLimit = 4096;
    Observer(native::Session session, std::uint64_t attempt, unsigned limit = defaultLimit)
        : session_(session), attempt_(attempt), limit_(limit) {
        if (!session.player || !session.generation || !attempt || !limit || limit > maximumLimit) {
            terminal_ = true; reason_ = Reason::invalid_configuration;
        }
    }
    Observer(const Observer&) = delete;
    Observer& operator=(const Observer&) = delete;
    bool terminal() const noexcept { return terminal_; }
    Reason reason() const noexcept { return reason_; }

    template<class Backend, class Sink>
    void sample(Backend& backend, const native::Entity& entity, std::uint64_t attempt,
        Walk walk, std::uint64_t callback, Sink&& sink) {
        // Check the actual thread/phase BEFORE touching mutable observer state.
        // The existing backend contract guarantees this check makes no GTA reads.
        if (!backend.nativeContext()) return;
        if (terminal_ || busy_) return;
        busy_ = true;
        struct Leave { bool& busy; ~Leave() { busy = false; } } leave{busy_};
        if (haveSample_ && callback > lastCallback_) {
            const auto distance = callback - firstCallback_;
            elapsed_ = distance >= limit_ ? limit_ : static_cast<unsigned>(distance);
        }
        auto finish = [&](Reason reason, native::Reason auth = native::Reason::none, bool sampled = false) {
            terminal_ = true; reason_ = reason;
            sink(Event{session_.generation, attempt_, elapsed_, limit_, 0, Slots::unknown,
                lastWalk_, reason, auth, sampled, arrived_, true});
        };
        if (entity.session != session_) { finish(Reason::session_changed); return; }
        if (attempt != attempt_) { finish(Reason::attempt_changed); return; }
        if (haveSample_ && callback <= lastCallback_) { finish(Reason::callback_regressed); return; }
        if (walk < lastWalk_ || walk > Walk::arrived) { finish(Reason::walk_regressed); return; }
        const bool walkChanged = walk != lastWalk_;
        lastWalk_ = walk; // Local state supplied by the existing movement observer.
        arrived_ = arrived_ || walk == Walk::arrived;
        if (!haveSample_) firstCallback_ = callback;
        if (elapsed_ >= limit_) { finish(Reason::callback_limit); return; } // No final native query.
        const auto observation = native::observeSlots(backend, entity);
        if (observation.slots == Slots::unknown) {
            finish(Reason::authentication_failed, observation.reason, true); return;
        }
        const bool changed = !haveSample_ || observation.slots != lastSlots_ ||
            observation.occupiedMask != lastMask_ || walkChanged;
        const auto reason = !haveSample_ ? Reason::initial : Reason::transition;
        haveSample_ = true; lastCallback_ = callback;
        lastSlots_ = observation.slots; lastMask_ = observation.occupiedMask;
        terminal_ = arrived_ && observation.slots == Slots::empty;
        if (changed || terminal_) {
            reason_ = terminal_ ? Reason::empty_after_arrival : reason;
            sink(Event{session_.generation, attempt_, elapsed_, limit_, lastMask_, lastSlots_,
                lastWalk_, reason_, native::Reason::none, true, arrived_, terminal_});
        }
    }
private:
    native::Session session_{};
    std::uint64_t attempt_ = 0, firstCallback_ = 0, lastCallback_ = 0;
    unsigned limit_ = 0, elapsed_ = 0, lastMask_ = 0;
    Slots lastSlots_ = Slots::unknown;
    Walk lastWalk_ = Walk::before_assignment;
    Reason reason_ = Reason::initial;
    bool haveSample_ = false, arrived_ = false, terminal_ = false, busy_ = false;
};
} // namespace grandmp::windows::task_release_diagnostics
