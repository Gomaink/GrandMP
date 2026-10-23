#pragma once
#include "native_task_release_diagnostics.hpp"
#include "ped_smoke_move.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <optional>

namespace grandmp::windows::native_diagnostic_binding {
namespace diag = task_release_diagnostics;
namespace native = continuous_remote_native;
namespace ps = ped_smoke;

inline std::optional<diag::Walk> walkState(ps::MoveState state) noexcept {
    switch (state) {
    case ps::MoveState::idle: return diag::Walk::before_assignment;
    case ps::MoveState::assigned: return diag::Walk::assigned;
    case ps::MoveState::moving: return diag::Walk::started;
    case ps::MoveState::arrived: return diag::Walk::arrived;
    default: return std::nullopt;
    }
}

// A stack-only borrowed view of the EXISTING RemoteEngine registry and primitives.
// Context must verify actual thread/caller/phase/PIN/preparation without GTA reads.
// Ready must prove executable/live task code/gameplay/streaming. Reader forwards
// ReadProcessMemory's success AND exact count. None is wired into an ASI here.
template<class Engine, class Context, class Ready, class Reader>
class RuntimeView final {
public:
    RuntimeView(Engine& engine, Context& context, Ready& ready, Reader& reader)
        : engine_(engine), context_(context), ready_(ready), reader_(reader) {}
    RuntimeView(const RuntimeView&) = delete;
    RuntimeView& operator=(const RuntimeView&) = delete;
    bool nativeContext() const { return context_(); }
    native::Entity identity() const {
        if (!nativeContext()) return {};
        const auto address = reinterpret_cast<std::uintptr_t>(engine_.owned);
        if (address > std::numeric_limits<std::uint32_t>::max()) return {};
        return {{engine_.key.generation, engine_.key.player}, static_cast<std::uint32_t>(address), engine_.reference};
    }
    bool matches(const native::Entity& entity) const { return nativeContext() && entity == identity(); }
    bool engineReady() { return nativeContext() && ready_(); }
    bool owns(std::uintptr_t ped) { return nativeContext() && engine_.owns(ped); }
    native::ReadResult readMemory(std::uint32_t address, void* output, std::size_t size) {
        if (!nativeContext()) return {false,0};
        return reader_(address,output,size);
    }
private:
    Engine& engine_;
    Context& context_;
    Ready& ready_;
    Reader& reader_;
};

enum class Failure { none, precondition, movement_failed, lifecycle_ended, outbox_full };
struct Record { diag::Event event{}; Failure bindingFailure = Failure::none; };
inline const char* name(Failure failure) noexcept {
    switch (failure) {
    case Failure::none: return "none";
    case Failure::precondition: return "precondition";
    case Failure::movement_failed: return "movement_failed";
    case Failure::lifecycle_ended: return "lifecycle_ended";
    default: return "outbox_full";
    }
}
inline constexpr std::size_t messageCapacity = 384;
// Deferred consumer ONLY. Never called by beforeAttempt/afterTick/publish.
inline bool format(char* output, std::size_t capacity, const Record& record) noexcept {
    if (!output || !capacity) return false;
    char event[diag::messageCapacity]{};
    if (!diag::format(event,sizeof(event),record.event)) { output[0]='\0'; return false; }
    const int count = std::snprintf(output,capacity,"binding=%s %s",name(record.bindingFailure),event);
    if (count < 0 || static_cast<std::size_t>(count) >= capacity) { output[0]='\0'; return false; }
    return true;
}

// Specific bounded SPSC outbox, not a logging service. Exactly one phase producer
// and one deferred consumer. No allocation, mutex, spin/retry or file operation.
class EventOutbox final {
public:
    static constexpr unsigned capacity = 16;
    static_assert(std::atomic<unsigned>::is_always_lock_free);
    bool push(const Record& event) noexcept {
        if (overflow_.load(std::memory_order_relaxed)) return false;
        const auto write = write_.load(std::memory_order_relaxed);
        const unsigned next = (write+1) % (capacity+1);
        if (next == read_.load(std::memory_order_acquire)) {
            overflow_.store(true,std::memory_order_release); return false;
        }
        events_[write] = event;
        write_.store(static_cast<unsigned>(next),std::memory_order_release); return true;
    }
    bool pop(Record& event) noexcept {
        const auto read = read_.load(std::memory_order_relaxed);
        if (read == write_.load(std::memory_order_acquire)) return false;
        event = events_[read];
        read_.store(static_cast<unsigned>((read+1) % events_.size()),std::memory_order_release); return true;
    }
    bool overflowed() const noexcept { return overflow_.load(std::memory_order_acquire); }
private:
    std::array<Record,capacity+1> events_{};
    std::atomic<unsigned> read_{0}, write_{0};
    std::atomic<bool> overflow_{false};
    static_assert(std::atomic<bool>::is_always_lock_free);
};

// Default disabled; true is instantiated ONLY in closed tests. No environment
// switch or existing physical build option includes/selects this foundation.
template<bool Enabled = false>
class Binding final {
public:
    // Producer-only status. The consumer uses ONLY pop()/overflowed().
    bool used() const noexcept { return used_; }
    bool terminal() const noexcept { return stopped_; }
    Failure failure() const noexcept { return failure_; }
    bool pop(Record& event) noexcept { return outbox_.pop(event); }
    bool overflowed() const noexcept { return outbox_.overflowed(); }

    // Controller exposes precisely rm::Controller's public state/ped/moveState/key.
    // Place before its original request=true Movement::tick, never instead of it.
    template<class View, class Controller>
    void beforeAttempt(View& view, const Controller& controller, std::uint64_t attempt,
        std::uint64_t callback, unsigned limit = diag::Observer::defaultLimit) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || used_) return;
            used_ = true; startCallback_ = callback;
            const auto entity = view.identity();
            reference_ = entity.reference; // Opaque registry handle, never a GTA address.
            initial_ = {entity.session.generation,attempt,0,limit,0,diag::Slots::unknown,
                diag::Walk::before_assignment,diag::Reason::initial,native::Reason::none,false,false,false};
            if (controller.moveState() != ps::MoveState::idle || controller.state() != ps::State::ped_created ||
                controller.ped() != entity.ped || !entity.ped || controller.key().generation != entity.session.generation ||
                controller.key().player != entity.session.player) {
                stopped_ = true; failure_ = Failure::precondition;
                auto event = initial_; event.terminal = true; publish(event,failure_); return;
            }
            observer_.emplace(entity.session,attempt,limit);
            if (observer_->terminal()) {
                stopped_ = true; failure_ = Failure::precondition;
                auto event = initial_; event.terminal = true; event.reason = diag::Reason::invalid_configuration;
                publish(event,failure_); return;
            }
            // Metadata-only notice. No pre-assignment slot sample: doing both
            // before/after probes in this callback would violate 2C monotonicity.
            publish(initial_);
        }
    }
    // Call AFTER the unchanged controller tick on EVERY admitted callback,
    // including after arrived (rm::Controller then stops ticking Movement).
    template<class View, class Controller>
    void afterTick(View& view, const Controller& controller, std::uint64_t attempt, std::uint64_t callback) {
        if constexpr (Enabled) {
            if (!view.nativeContext() || !used_ || stopped_) return;
            const unsigned elapsed = callback >= startCallback_ ?
                static_cast<unsigned>((std::min)(callback-startCallback_,std::uint64_t{initial_.limit})) : 0;
            auto stop = [&](diag::Reason reason, native::Reason auth = native::Reason::none, Failure failure = Failure::none) {
                auto event = initial_; event.terminal = true; event.walk = lastWalk_;
                event.afterArrival = lastWalk_ == diag::Walk::arrived; event.elapsedCallbacks = elapsed;
                event.reason = reason; event.authentication = auth;
                stopped_ = true; failure_ = failure; publish(event,failure);
            };
            if (callback < startCallback_) { stop(diag::Reason::callback_regressed); return; }
            if (elapsed >= initial_.limit) { stop(diag::Reason::callback_limit); return; }
            const auto mapped = walkState(controller.moveState());
            if (controller.state() != ps::State::ped_created || !mapped) {
                const bool lost = controller.state() == ps::State::ownership_lost;
                stop(lost ? diag::Reason::authentication_failed : diag::Reason::transition,
                    lost ? native::Reason::ownership : native::Reason::none,
                    mapped ? Failure::lifecycle_ended : Failure::movement_failed); return;
            }
            const auto entity = view.identity();
            if (controller.key().generation != entity.session.generation || controller.key().player != entity.session.player) {
                stop(diag::Reason::session_changed,native::Reason::identity); return;
            }
            if (!entity.ped || controller.ped() != entity.ped || entity.reference != reference_) {
                stop(diag::Reason::authentication_failed,native::Reason::identity); return;
            }
            lastWalk_ = *mapped;
            observer_->sample(view,entity,attempt,*mapped,callback,[&](const diag::Event& event) noexcept {
                auto copy = event; copy.elapsedCallbacks = elapsed; publish(copy);
            });
            stopped_ = stopped_ || observer_->terminal();
        }
    }
private:
    void publish(const diag::Event& event, Failure failure = Failure::none) noexcept {
        if (!outbox_.push({event,failure})) { stopped_ = true; failure_ = Failure::outbox_full; }
    }
    EventOutbox outbox_;
    std::optional<diag::Observer> observer_;
    diag::Event initial_{};
    diag::Walk lastWalk_ = diag::Walk::before_assignment;
    std::uint64_t startCallback_ = 0;
    std::int32_t reference_ = -1;
    Failure failure_ = Failure::none;
    bool used_ = false, stopped_ = false;
};
} // namespace grandmp::windows::native_diagnostic_binding
