#pragma once
#include "grandmp/core/remote_player_controller.hpp"
#include <atomic>

namespace grandmp::windows::remote_player_binding::minimal_lifecycle {
enum class State { empty, removal_requested, quiescent, reference_unlinked, world_detached,
    ped_destroyed, resources_released, removed_verified };
enum class Boundary { none, clean_reference, world_remove, deleting_destructor, data_destructor };
enum class Failure { none, admission, reference, world, destruction, resources, verification, interrupted };
// Native-owner metadata only. Resident, immutable after failure/completion. No pointers.
struct Tombstone {
    core::RemotePlayerKey key{};
    State confirmed=State::empty;
    Boundary entered=Boundary::none;
    Failure failure=Failure::none;
    bool requested=false;
    bool request(core::RemotePlayerKey identity) noexcept {
        if(requested)return false;
        requested=true;key=identity;confirmed=State::removal_requested;return true;
    }
    bool fail(Failure why) noexcept {
        if(failure==Failure::none)failure=why;
        return false;
    }
    void finishUnknown() noexcept {
        if(confirmed!=State::removed_verified && failure==Failure::none)fail(Failure::interrupted);
    }
};
// One producer executes on the authenticated game thread. The resident wrapper
// checks this before looking at the retired entity or invoking ProcessControl.
class DispatchBarrier final {
public:
    bool closed() const noexcept {return closed_.load(std::memory_order_acquire);}
    void close() noexcept {closed_.store(true,std::memory_order_release);}
private:
    std::atomic<bool> closed_{false};
};
class Edge final {
public:
    bool poll(bool pressed) noexcept {
        const bool rising=pressed && !held_;held_=pressed;
        if(!rising || used_)return false;
        used_=true;return true; // A refusal consumes the only request too.
    }
private:
    bool held_=false,used_=false;
};
// Completion only schedules a preflight; it never authorizes native cleanup.
enum class Window { waiting, complete, failed };
enum class Scheduled { idle, pending, consumed, terminal };
enum class Opportunity { wait, attempt, refused };
class ArmedRemoval final {
public:
    // One six-second experiment plus the existing three-second heartbeat bound.
    static constexpr std::uint64_t maximumWaitMs=9000;
    Scheduled state() const noexcept {return state_;}
    Opportunity poll(bool pressed,bool live,core::RemotePlayerKey key,Window window,std::uint64_t now) noexcept {
        if(state_==Scheduled::consumed || state_==Scheduled::terminal)return Opportunity::wait;
        if(edge_.poll(pressed)){
            if(!live || !key.player || !key.generation)return refuse();
            key_=key;since_=now;state_=Scheduled::pending;
        }
        if(state_!=Scheduled::pending)return Opportunity::wait;
        if(!live || key!=key_ || window==Window::failed || now<since_ || now-since_>maximumWaitMs)return refuse();
        if(window!=Window::complete)return Opportunity::wait;
        state_=Scheduled::consumed;return Opportunity::attempt; // Never wait for a second safe-looking frame.
    }
    void cancel() noexcept {if(state_==Scheduled::pending || state_==Scheduled::consumed)state_=Scheduled::terminal;}
private:
    Opportunity refuse() noexcept {state_=Scheduled::terminal;return Opportunity::refused;}
    Edge edge_;
    core::RemotePlayerKey key_{};
    std::uint64_t since_=0;
    Scheduled state_=Scheduled::idle;
};
// Shared by the real native calls and directed fault tests. Each irreversible
// boundary is recorded BEFORE its call. No compensation, retry or manual unlink.
template<class Operations> bool removeOnce(Tombstone& t,core::RemotePlayerKey key,Operations& op) {
    if(!t.request(key))return false;
    try {
        if(!op.preflight())return t.fail(Failure::admission);
        t.confirmed=State::quiescent;t.entered=Boundary::clean_reference;
        op.cleanReference();
        if(!op.referenceUnlinked())return t.fail(Failure::reference);
        t.confirmed=State::reference_unlinked;t.entered=Boundary::world_remove;
        op.removeWorld();
        if(!op.worldDetached())return t.fail(Failure::world);
        t.confirmed=State::world_detached;t.entered=Boundary::deleting_destructor;
        op.destroyPed();
        if(!op.pedDestroyed())return t.fail(Failure::destruction);
        t.confirmed=State::ped_destroyed;t.entered=Boundary::data_destructor;
        op.destroyData();
        if(!op.resourcesReleased())return t.fail(Failure::resources);
        t.confirmed=State::resources_released;
        if(!op.verified())return t.fail(Failure::verification);
        t.confirmed=State::removed_verified;return true;
    } catch(...) {t.finishUnknown();throw;}
}
}
