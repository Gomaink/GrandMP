#pragma once

#include "grandmp/core/remote_player_controller.hpp"
#include "grandmp/windows/native_player_input.hpp"
#include <array>
#include <limits>
#include <new>
#include <thread>

namespace grandmp::windows::remote_player_binding {

#if defined(GRANDMP_NATIVE_REMOTE_PLAYER_BINDING_BUILD) && GRANDMP_NATIVE_REMOTE_PLAYER_BINDING_BUILD == 1
inline constexpr bool buildEnabled = true;
#else
inline constexpr bool buildEnabled = false;
#endif

enum class Phase { pre_world, original_process_control };
enum class State { empty, live, retired, blocked, ownership_lost, indeterminate, poisoned };
enum class Result { closed, context_refused, no_input, occupied, stale, blocked_contract,
    created, updated, removed, ownership_lost, indeterminate, duplicate, reentrant,
    processed, context_failed, original_failed, restore_failed };

// Private to the native consumer. Never put this record in Controller/UDP/logs.
struct Identity {
    std::uint32_t ped = 0, playerData = 0, pad = 0;
    std::int32_t handle = -1;
    unsigned slot = std::numeric_limits<unsigned>::max(); // No PlayerInfo slot for pool-owned backends.
    bool operator==(const Identity&) const = default;
};
struct Record {
    core::RemotePlayerKey key{};
    Identity entity{};
    std::uint32_t reference = 0; // Stable engine-reference cell, not a historical pointer to dereference.
};
struct Dispatch {
    core::RemotePlayerKey key{};
    std::uint32_t ped = 0;
    std::uint64_t serial = 0; // Authenticated ORIGINAL dispatch, never a UDP sequence/frame guess.
};
struct Controls {
    core::PlayerControls previous{}, current{};
    float viewHeading = 0;
    OnFootPadStates pad{}; // Prepared values; never automatically written into GTA.
};
enum class InputDisposition { fresh, neutral_missing, neutral_timeout, neutral_stale, neutral_contended };
// Optional value-only observation. Marks existing calls; never probes the engine.
enum class ProcessStep : unsigned { none, gate, owner, context, reentrancy, live, serial,
    replay, identity, dispatch, ownership, capture, capture_size, pad, apply,
    before_original, original, restore, after_original, complete };
struct ProcessObservation {
    ProcessStep body = ProcessStep::none, primary = ProcessStep::none;
    std::uint32_t entered = 0, passed = 0;
    bool cppException = false;
    void enter(ProcessStep step) noexcept {
        entered |= std::uint32_t{1} << static_cast<unsigned>(step);
        if (step!=ProcessStep::restore) body=step;
    }
    void finish(ProcessStep step,bool okay) noexcept {
        if (okay) passed |= std::uint32_t{1} << static_cast<unsigned>(step);
        else if (step!=ProcessStep::restore && primary==ProcessStep::none) primary=step;
    }
    bool check(ProcessStep step,bool okay) noexcept { enter(step);finish(step,okay);return okay; }
};
struct ContextImage {
    // Backend authenticates the full layout and every required global. Not a
    // copied CPad definition; unproven camera/aim state cannot be omitted.
    std::array<std::byte,2048> bytes{};
    std::size_t used = 0;
};
enum class Creation { absent, complete, indeterminate };
enum class Removal { complete, indeterminate };

class Api {
public:
    virtual ~Api() = default;
    // No engine reads in context(). Wrong phase/thread must stop before probes.
    virtual bool context(Phase) const noexcept = 0;
    // Requires proven backend-owned storage/capacity, resident code, admitted
    // executable/live code, allocator/lifecycle ABI and no alias to CJ. Legacy
    // backends additionally authenticate their PlayerInfo slot; it is not a
    // requirement for independent pool-owned players.
    virtual bool factoryProven() const noexcept = 0;
    virtual bool removalProven() const noexcept = 0;
    virtual bool replayProven() const noexcept = 0;
    // create MUST publish any allocated entity in this stable cell immediately;
    // partial/unknown results retain it, block admission and never auto-delete.
    // No snapshot coordinates are a native spawn/applyPose command. Factory
    // supplies its own admitted spawn conditions, independently of UDP XYZ.
    virtual Creation create(core::RemotePlayerKey, Record&) = 0;
    // Must authenticate key/session, reference node, tagged pool handle,
    // backend-owned player-data/pad identities and ownership anew (including a
    // PlayerInfo slot only for slot-backed implementations). Historical
    // addresses are only comparisons, never authority to dereference memory.
    virtual bool authenticate(const Record&) noexcept = 0;
    virtual Removal remove(Record&) = 0;
    // Requires returned destruction + released tagged pool handle (and slot,
    // where the backend uses one) AND old
    // registered reference unlinked. reference==0 alone never permits reuse.
    virtual bool removed(const Record& before, const Record& after) noexcept = 0;
    // Proof of this exact original dispatch, its ABI, exclusive context writers
    // and complete pad/focus/camera/aim dependencies; not a pre-world callback.
    virtual bool dispatch(const Record&,const Dispatch&) noexcept = 0;
    // Read-only all-or-nothing capture. No partial mutation before apply().
    virtual bool capture(ContextImage&) noexcept = 0;
    virtual bool apply(const Record&,const Controls&) = 0;
    virtual bool original(const Record&) = 0; // Wrapper status, NOT a guessed GTA return ABI.
    virtual bool restore(const ContextImage&) noexcept = 0; // Includes verification of restored values.
};

// Win32 __finally boundary. It attempts restore on C++/SEH unwinding and does
// NOT swallow an engine access violation or invent successful recovery.
void withFinally(void (*body)(void*), void (*restore)(void*) noexcept, void* context);

template<bool Enabled = buildEnabled>
class Binding final {
public:
    // Prepared outside gameplay; retained until process exit, including unknown
    // creation/removal and callbacks still in flight. No public destructor/move.
    static Binding* prepare(std::thread::id nativeOwner) noexcept {
        static Binding* instance=new (std::nothrow) Binding(nativeOwner);
        return instance && instance->owner_==nativeOwner ? instance : nullptr;
    }
    Binding(const Binding&) = delete;
    Binding& operator=(const Binding&) = delete;
    State state() const noexcept { return state_; } // Native-owner only.
    core::RemotePlayerKey key() const noexcept { return record_.key; }
    // Comparison only on the native owner thread; never exported to a worker.
    std::uint32_t entityAddress() const noexcept { return record_.entity.ped; }
    std::int32_t entityHandle() const noexcept { return record_.entity.handle; } // Native-owner value, never a pointer.
    // Read-only routing proof for the existing original-control wrapper. A
    // historical address alone must never select remote input or pose writes.
    bool authenticatesDispatchEntity(std::uint32_t ped, Api& api) noexcept {
        if constexpr (!Enabled) return false;
        return owner() && !busy_ && state_==State::live && ped &&
            ped==record_.entity.ped && record_.key.player && record_.key.generation &&
            api.context(Phase::original_process_control) && api.authenticate(record_);
    }
    const char* retirementReason() const noexcept { return retirementReason_; }
    InputDisposition inputDisposition() const noexcept { return inputDisposition_; }

    Result consume(core::RemotePlayerController& controller, core::RemoteRoster::Time now, Api& api) {
        if constexpr (!Enabled) return Result::closed;
        if (!owner() || !api.context(Phase::pre_world)) return Result::context_refused;
        if (busy_) return Result::reentrant;
        if (terminal()) return Result::indeterminate;
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
        if (removalRequested_) return Result::blocked_contract;
#endif
        core::RemotePlayerFrames frames;
        if (!controller.tryRead(now,frames)) { current_={};inputDisposition_=InputDisposition::neutral_contended;return Result::no_input; }
        const core::RemotePlayerFrame* selected=nullptr;
        if (frames.count>frames.players.size()) return Result::no_input;
        for (std::size_t i=0;i<frames.count;++i) {
            const auto& f=frames.players[i];
            if (state_==State::live ? f.key==record_.key : f.freshness==core::ControlFreshness::fresh) {
                selected=&f; break;
            }
        }
        if (state_==State::live) {
            if (!api.authenticate(record_)) { current_={};state_=State::ownership_lost;return Result::ownership_lost; }
            if (!selected) {
                retirementReason_=frames.recipientSession ? "entity_absent_from_scene" : "receiver_session_unavailable";
                for (std::size_t n=0;n<frames.endedCount && n<frames.ended.size();++n)
                    if (frames.ended[n].key==record_.key) retirementReason_=core::remoteEndName(frames.ended[n].reason);
                return retire(api); // Leave/new generation first, no respawn in this tick.
            }
            if (selected->sequence<sequence_) return Result::stale;
            inputDisposition_=selected->freshness==core::ControlFreshness::fresh ? InputDisposition::fresh : InputDisposition::neutral_timeout;
            current_=selected->freshness==core::ControlFreshness::fresh ? selected->reference.controls : core::PlayerControls{};
            viewHeading_=selected->reference.viewHeading; sequence_=selected->sequence;
            return Result::updated;
        }
        if (!selected) return Result::no_input;
        if (!selected->key.player || !selected->key.generation || selected->key.generation<=generationFloor_ ||
            !selected->sequence || !core::validOnFoot(selected->reference)) return Result::stale;
        if (!api.factoryProven()) { state_=State::blocked;return Result::blocked_contract; }
        busy_=true; state_=State::indeterminate;
        record_={selected->key,{},{}};generationFloor_=selected->key.generation;
        Creation result=Creation::indeterminate;
        try { result=api.create(record_.key,record_); }
        catch (...) { busy_=false;return Result::indeterminate; }
        busy_=false;
        if (result==Creation::absent && !record_.reference && !record_.entity.ped) {
            state_=State::blocked;return Result::blocked_contract; // No automatic retry.
        }
        if (result!=Creation::complete || record_.key!=selected->key || !record_.reference || !record_.entity.ped ||
            record_.reference!=record_.entity.ped || !api.authenticate(record_)) return Result::indeterminate;
        current_=selected->reference.controls;viewHeading_=selected->reference.viewHeading;
        sequence_=selected->sequence;previous_={};lastDispatch_=0;state_=State::live;
        inputDisposition_=InputDisposition::fresh;retirementReason_="none";
        return Result::created;
    }

#if defined(GRANDMP_INDEPENDENT_PLAYER_UDP_BUILD) && GRANDMP_INDEPENDENT_PLAYER_UDP_BUILD == 1
    // Input-only milestone: even an explicit request cannot consult cleanup providers.
    Result requestRemoval(Api&) {
        if constexpr (!Enabled) return Result::closed;
        return owner() ? Result::blocked_contract : Result::context_refused;
    }
#elif defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
    // Minimal profile: one live request, no partial-construction cleanup.
    Result requestRemoval(Api& api) {
        if constexpr (!Enabled) return Result::closed;
        if (!owner() || !api.context(Phase::pre_world)) return Result::context_refused;
        if (busy_) return Result::reentrant;
        if (removalRequested_) return Result::blocked_contract;
        removalRequested_=true;
        const bool live=state_==State::live;state_=State::indeterminate;
        if (!live || !api.authenticate(record_)) return Result::blocked_contract;
        retirementReason_="operator_removal_request";
        return retire(api);
    }

#else
    // Explicit operator request, including a terminal but authenticated partial build.
    // No recovery, retries or cleanup of an uncorrelated factory result.
    Result requestRemoval(Api& api) {
        if constexpr (!Enabled) return Result::closed;
        if (!owner() || !api.context(Phase::pre_world)) return Result::context_refused;
        if (busy_) return Result::reentrant;
        if ((state_!=State::live && state_!=State::indeterminate) || !api.authenticate(record_))
            return Result::blocked_contract;
        retirementReason_="operator_removal_request";
        return retire(api);
    }

#endif

    // Only an authenticated original CPlayerPed dispatch may enter. This is not
    // called by consume()/pre_world and installs no hook or substitute scheduler.
    Result process(core::RemotePlayerController& controller, core::RemoteRoster::Time now,
                   const Dispatch& dispatch, Api& api, ProcessObservation* observation=nullptr) {
        if (observation) *observation={};
        const auto check=[observation](ProcessStep step,bool okay) noexcept {
            return observation ? observation->check(step,okay) : okay;
        };
        const auto probe=[observation](ProcessStep step,auto&& call) {
            if (observation) observation->enter(step);
            const bool okay=call();
            if (observation) observation->finish(step,okay);
            return okay;
        };
        if constexpr (!Enabled) { check(ProcessStep::gate,false);return Result::closed; }
        if (!check(ProcessStep::owner,owner()) || !probe(ProcessStep::context,[&]{return api.context(Phase::original_process_control);})) return Result::context_refused;
        if (!check(ProcessStep::reentrancy,!busy_)) return Result::reentrant;
        if (!check(ProcessStep::live,state_==State::live)) return Result::indeterminate;
        if (!check(ProcessStep::serial,dispatch.serial && dispatch.serial>lastDispatch_)) return Result::duplicate;
        if (!probe(ProcessStep::replay,[&]{return api.replayProven();})) return Result::blocked_contract;
        if (!check(ProcessStep::identity,dispatch.key==record_.key && dispatch.ped==record_.entity.ped) ||
            !probe(ProcessStep::dispatch,[&]{return api.dispatch(record_,dispatch);}) || !probe(ProcessStep::ownership,[&]{return api.authenticate(record_);})) {
            current_={};state_=State::ownership_lost;return Result::ownership_lost;
        }
        // Re-read the real controller at the ORIGINAL dispatch. Pre-world input
        // is not a freshness lease. Contention/expired input always means neutral.
        core::RemotePlayerFrames frames;
        current_={};
        inputDisposition_=InputDisposition::neutral_contended;
        if (controller.tryRead(now,frames)) {
            const core::RemotePlayerFrame* frame=nullptr;
            for (std::size_t n=0;n<frames.count && n<frames.players.size();++n)
                if (frames.players[n].key==record_.key) { frame=&frames.players[n];break; }
            // A legitimate ORIGINAL dispatch must keep running even if input
            // vanished between pre-world and ProcessControl. Neutralize; removal
            // stays in pre-world. Never let an old key control this live record.
            inputDisposition_=InputDisposition::neutral_missing;
            if (frame) {
                inputDisposition_=InputDisposition::neutral_stale;
                if (frame->sequence>=sequence_ && core::validOnFoot(frame->reference)) {
                    inputDisposition_=InputDisposition::neutral_timeout;
                    if (frame->freshness==core::ControlFreshness::fresh) {
                        current_=frame->reference.controls;inputDisposition_=InputDisposition::fresh;
                    }
                    viewHeading_=frame->reference.viewHeading;sequence_=frame->sequence;
                }
            }
        }
        ContextImage saved;
        if (!probe(ProcessStep::capture,[&]{return api.capture(saved);}) || !check(ProcessStep::capture_size,saved.used && saved.used<=saved.bytes.size())) {state_=State::poisoned;return Result::context_failed;}
        busy_=true;state_=State::poisoned;lastDispatch_=dispatch.serial;
        Invocation invocation{*this,api,saved,observation};
        try { withFinally(body,restore,&invocation); }
        catch (...) { // __finally already attempted restoration; no retry.
            if (observation) {
                observation->cppException=true;
                if (observation->primary==ProcessStep::none) observation->primary=observation->body;
            }
            if (!invocation.restored) return Result::restore_failed;
            return invocation.applied ? Result::original_failed : Result::context_failed;
        }
        if (!invocation.restored) return Result::restore_failed;
        if (!invocation.applied) return Result::context_failed;
        if (!invocation.returned) return Result::original_failed;
        if (!probe(ProcessStep::after_original,[&]{return api.authenticate(record_);})) { state_=State::ownership_lost;return Result::ownership_lost; }
        previous_=current_;state_=State::live;
        check(ProcessStep::complete,true);
        return Result::processed;
    }
private:
    explicit Binding(std::thread::id owner) : owner_(owner) {}
    ~Binding() = default;
    bool owner() const noexcept { return owner_!=std::thread::id{} && owner_==std::this_thread::get_id(); }
    bool terminal() const noexcept { return state_==State::blocked || state_==State::ownership_lost || state_==State::indeterminate || state_==State::poisoned; }
    Result retire(Api& api) {
#if defined(GRANDMP_INDEPENDENT_PLAYER_UDP_BUILD) && GRANDMP_INDEPENDENT_PLAYER_UDP_BUILD == 1
        (void)api;current_={};inputDisposition_=InputDisposition::neutral_missing;
        return Result::no_input; // Keep the authenticated world object processing neutral; no destruction/replacement.
#else
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
        removalRequested_=true;state_=State::indeterminate;
#endif
        current_={};
        if (!api.removalProven()) {state_=State::indeterminate;return Result::blocked_contract;}
        busy_=true;state_=State::indeterminate;
        const Record before=record_; // Retain old handle/key for an authenticated destruction postcondition.
        Removal result=Removal::indeterminate;
        try { result=api.remove(record_); } catch (...) {busy_=false;return Result::indeterminate;}
        busy_=false;
        if (result!=Removal::complete || record_.key!=before.key || record_.entity!=before.entity ||
            !api.removed(before,record_)) return Result::indeterminate;
        // Only now release our record. No dereference after native destruction.
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
        previous_={};sequence_=0;state_=State::retired; // Retain logical identity; never readmit.
#else
        record_={};previous_={};sequence_=0;state_=State::retired;
#endif
        return Result::removed;
#endif
    }
    struct Invocation { Binding& binding;Api& api;const ContextImage& saved;ProcessObservation* observation=nullptr;bool applied=false,returned=false,restored=false; };
    static void body(void* value) {
        auto& i=*static_cast<Invocation*>(value);auto& b=i.binding;
        Controls input{b.previous_,b.current_,b.viewHeading_};
        if (i.observation) i.observation->enter(ProcessStep::pad);
        const bool prepared=prepareOnFootPad(input.previous,input.current,input.pad);
        if (i.observation) i.observation->finish(ProcessStep::pad,prepared);
        if (!prepared) return;
        if (i.observation) i.observation->enter(ProcessStep::apply);
        i.applied=i.api.apply(b.record_,input);
        if (i.observation) i.observation->finish(ProcessStep::apply,i.applied);
        if (i.applied) {
            if (i.observation) i.observation->enter(ProcessStep::before_original);
            const bool authenticated=i.api.authenticate(b.record_);
            if (i.observation) i.observation->finish(ProcessStep::before_original,authenticated);
            if (authenticated) {
                if (i.observation) i.observation->enter(ProcessStep::original);
                i.returned=i.api.original(b.record_);
                if (i.observation) i.observation->finish(ProcessStep::original,i.returned);
            }
        }
    }
    static void restore(void* value) noexcept {
        auto& i=*static_cast<Invocation*>(value);
        if (i.observation) i.observation->enter(ProcessStep::restore);
        i.restored=i.api.restore(i.saved);
        if (i.observation) i.observation->finish(ProcessStep::restore,i.restored);
        i.binding.busy_=false; // Residency persists even if unwind escapes the caller.
    }
    std::thread::id owner_;
    Record record_{};
    core::PlayerControls current_{},previous_{};
    float viewHeading_=0;
    std::uint64_t generationFloor_=0,lastDispatch_=0;
    std::uint32_t sequence_=0;
    State state_=State::empty;
#if defined(GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD) && GRANDMP_MINIMAL_NATIVE_LIFECYCLE_BUILD == 1
    bool removalRequested_=false;
#endif
    bool busy_=false;
    const char* retirementReason_="none";
    InputDisposition inputDisposition_=InputDisposition::neutral_missing;
};

} // namespace grandmp::windows::remote_player_binding
