#include "grandmp/core/remote_player_controller.hpp"
#include <limits>

namespace grandmp::core {

RemotePlayerController::Entry* RemotePlayerController::find(std::uint64_t id) noexcept {
    for (auto& e : entries_) if (e.frame.key.player==id) return &e;
    return nullptr;
}
RemotePlayerController::Entry* RemotePlayerController::admit(std::uint64_t id) noexcept {
    if (!id || id==recipient_ || !recipient_) return nullptr;
    if (auto* e=find(id)) return e;
    if (nextGeneration_==std::numeric_limits<std::uint64_t>::max()) return nullptr;
    for (auto& e : entries_) if (!e.frame.key.player) {
        e={}; e.frame.key={id,++nextGeneration_}; return &e;
    }
    return nullptr;
}
void RemotePlayerController::rememberEnd(Entry& entry,RemoteEndReason reason) noexcept {
    if (entry.frame.key.player) {
        ended_[endedCursor_]={entry.frame.key,reason};
        endedCursor_=(endedCursor_+1)%ended_.size();
        if (endedCount_<ended_.size()) ++endedCount_;
    }
    entry={};
}
void RemotePlayerController::endRemote(std::uint64_t id,RemoteEndReason reason) noexcept {
    std::lock_guard lock(mutex_);
    if (auto* e=find(id)) rememberEnd(*e,reason);
}
void RemotePlayerController::beginSession(std::uint64_t recipient,RemoteEndReason reason) noexcept {
    {
        std::lock_guard lock(mutex_);
        for (auto& e:entries_) rememberEnd(e,reason);
        recipient_=0; // Publish invalidation before any old Scene callback.
    }
    scene_.reset(*this); // Scene callbacks take the lock; never hold it across a callback.
    std::lock_guard lock(mutex_);
    recipient_=recipient; // generation counter deliberately NEVER resets.
}
bool RemotePlayerController::receive(const protocol::RemoteOnFoot& s,
                                     const RemoteRoster& roster, Time now) noexcept {
    // Only samples admitted by the existing roster can reach a representation.
    const auto* player=roster.get(s.player_id);
    if (!player || !player->hasPosition || player->sequence!=s.sequence ||
        player->x!=s.state.x || player->y!=s.state.y || player->z!=s.state.z ||
        !s.sequence || !validOnFoot(s.state)) return false;
    std::lock_guard lock(mutex_);
    if (!s.recipient_session || s.recipient_session!=recipient_) return false;
    auto* e=admit(s.player_id);
    if (!e || s.sequence<=e->frame.sequence || (e->frame.sequence && now<e->received)) return false;
    e->frame.sequence=s.sequence; e->frame.reference=s.state;
    e->frame.freshness=ControlFreshness::fresh; e->received=now;
    return true;
}
void RemotePlayerController::synchronize(const RemoteRoster& roster, Time now) {
    scene_.synchronize(roster,now,*this);
    std::lock_guard lock(mutex_);
    for (auto& e : entries_) if (e.frame.key.player && !roster.get(e.frame.key.player)) rememberEnd(e,RemoteEndReason::roster_absent);
}
void RemotePlayerController::spawn(const RemotePose& pose) noexcept {
    std::lock_guard lock(mutex_);
    if (auto* e=admit(pose.id)) { e->frame.presentation=pose; e->visible=true; }
}
void RemotePlayerController::move(const RemotePose& pose) noexcept {
    std::lock_guard lock(mutex_);
    if (auto* e=find(pose.id)) e->frame.presentation=pose;
}
void RemotePlayerController::despawn(std::uint64_t id) noexcept {
    std::lock_guard lock(mutex_);
    if (auto* e=find(id)) rememberEnd(*e,RemoteEndReason::roster_absent);
}
bool RemotePlayerController::tryRead(Time now, RemotePlayerFrames& out) const noexcept {
    out={};
    std::unique_lock lock(mutex_,std::try_to_lock);
    if (!lock.owns_lock()) return false; // Caller must NOT replay a stale cached frame.
    out.recipientSession=recipient_;
    out.endedCount=endedCount_;out.ended=ended_;
    for (const auto& e : entries_) if (e.visible) {
        auto frame=e.frame;
        if (frame.sequence && (now<e.received || now-e.received>=inputTimeout)) {
            frame.freshness=ControlFreshness::input_timeout;
            frame.reference.controls={}; // Lost UDP cannot hold walking input forever.
        }
        out.players[out.count++]=frame;
    }
    return true;
}

} // namespace grandmp::core
