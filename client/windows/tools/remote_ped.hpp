#pragma once
#include "ped_smoke_state.hpp"
#include "grandmp/core/remote_player_scene.hpp"
#include "grandmp/core/protocol.hpp"
#include <array>
#include <limits>
#include <mutex>

namespace grandmp::windows::remote_ped {
namespace ps = ped_smoke;
// The separate ASI additionally needs the existing physical admission gate.
#if defined(GRANDMP_REMOTE_PED_PHYSICAL_BUILD) && GRANDMP_REMOTE_PED_PHYSICAL_BUILD == 1
inline constexpr bool physicalExecutionApproved = ps::physicalExecutionApproved;
#else
inline constexpr bool physicalExecutionApproved = false;
#endif
struct Key {
    std::uint64_t generation = 0, player = 0;
    bool operator==(const Key&) const = default;
};
struct Intent {
    std::uint64_t revision = 1;
    Key key{};
    core::RemotePose pose{}; // Diagnostic data only, never a native pointer/position command.
    bool present = false, blocked = false;
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
    core::RemotePose initialPose{}; // Immutable first validated pose, before interpolation can advance.
    std::uint64_t sampleRevision = 0; // Includes ordered Scene interpolation within one UDP sequence.
#endif
};

// Specific one-slot handoff for this experiment. The ObserverNetwork worker is
// the only producer; its callbacks NEVER call an engine/backend operation.
class IntentSink final : public core::IRemotePlayerSceneSink {
public:
    void spawn(const core::RemotePose& pose) noexcept override {
        std::lock_guard lock(mutex_);
        if (intent_.blocked || !valid(pose)) return;
        if (intent_.present) {
            if (pose.id == intent_.key.player && pose.sequence >= intent_.pose.sequence) publishPose(pose);
            return; // A second remote is not promoted automatically later.
        }
        if (retired(pose.id)) return; // v1 cannot disambiguate late datagrams for a reused ID.
        if (!advance() || intent_.key.generation == std::numeric_limits<std::uint64_t>::max()) { block(); return; }
        ++intent_.key.generation;
        intent_.key.player = pose.id;
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        intent_.initialPose = pose;
#endif
        publishPose(pose);
        if (!intent_.blocked) intent_.present = true;
    }
    void move(const core::RemotePose& pose) noexcept override {
        std::lock_guard lock(mutex_);
        if (!intent_.blocked && intent_.present && pose.id == intent_.key.player && valid(pose) && pose.sequence >= intent_.pose.sequence)
            publishPose(pose); // No native operation; lifecycle revision stays separate.
    }
    void despawn(std::uint64_t player) noexcept override {
        std::lock_guard lock(mutex_);
        if (!intent_.present || player != intent_.key.player) return;
        retire(player);
        intent_.present = false;
        (void)advance();
    }
    void resetSession() noexcept override {
        std::lock_guard lock(mutex_);
        if (intent_.present) retire(intent_.key.player);
        intent_.present = false;
        if (intent_.key.generation == std::numeric_limits<std::uint64_t>::max()) block();
        else ++intent_.key.generation;
        (void)advance();
    }
    Intent snapshot() const noexcept { std::lock_guard lock(mutex_); return intent_; }
private:
    void publishPose(const core::RemotePose& pose) noexcept {
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        if (intent_.pose.id == pose.id && intent_.pose.sequence == pose.sequence && intent_.pose.x == pose.x && intent_.pose.y == pose.y && intent_.pose.z == pose.z) return;
        if (intent_.sampleRevision == std::numeric_limits<std::uint64_t>::max()) { block(); (void)advance(); return; }
        ++intent_.sampleRevision;
#endif
        intent_.pose = pose;
    }
    static bool valid(const core::RemotePose& pose) noexcept {
        return pose.id && pose.sequence && protocol::validCoordinates(pose.x, pose.y, pose.z);
    }
    bool retired(std::uint64_t id) const noexcept {
        for (unsigned i = 0; i < retiredCount_; ++i) if (retired_[i] == id) return true;
        return false;
    }
    void retire(std::uint64_t id) noexcept {
        if (retired(id)) return;
        if (retiredCount_ == retired_.size()) { block(); return; }
        retired_[retiredCount_++] = id;
    }
    void block() noexcept { intent_.blocked = true; intent_.present = false; }
    bool advance() noexcept {
        if (intent_.revision == std::numeric_limits<std::uint64_t>::max()) { block(); return false; }
        ++intent_.revision;
        return true;
    }
    mutable std::mutex mutex_;
    Intent intent_{};
    std::array<std::uint64_t, 64> retired_{}; // Bounded; ambiguous reuse/exhaustion is fail-closed.
    unsigned retiredCount_ = 0;
};

// Only called from the admitted native phase. Uses precisely the existing
// one-shot Experiment; it is rearmed only for a NEW generation after confirmed
// removal (or a terminal attempt that never allocated any PED).
class Controller final {
public:
    ps::State state() const noexcept { return entity_.state(); }
    std::uintptr_t ped() const noexcept { return entity_.ped(); }
    Key key() const noexcept { return key_; }
    const ps::Experiment& entity() const noexcept { return entity_; }
    bool poisoned() const noexcept { return poisoned_; }
    // Native consumer only: terminal code failure without querying/removing PEDs.
    void disable() noexcept { poisoned_ = true; entity_.fail(); }
    template<class Engine>
    void tick(Engine& engine, const Intent& incoming, bool nativeThread, bool gameplay, std::uint64_t now) {
        if (busy_ || poisoned_ || !nativeThread || !gameplay) return;
        busy_ = true;
        struct Leave { bool& busy; ~Leave() { busy = false; } } leave{busy_};
        if (incoming.revision > desired_.revision) desired_ = incoming;
        // Older snapshots, including old leaves, never replace the current intent.
        const bool wanted = desired_.present && !desired_.blocked && desired_.key.player && desired_.key.generation;
        if (occupied_ && (!wanted || desired_.key != key_)) {
            entity_.tick(engine, true, true, true, now); // Cancels even before RequestModel.
            if (entity_.state() == ps::State::removed || (entity_.terminal() && !entity_.ped())) occupied_ = false;
            else if (entity_.terminal()) poisoned_ = true; // Unknown/live failed identity: no deletion/replacement.
            return; // Never allocate a replacement in the cleanup callback.
        }
        if (!occupied_) {
            if (!wanted || desired_.key.generation <= consumed_) return;
            consumed_ = desired_.key.generation; // Consume before engine calls, including failed begin.
            key_ = desired_.key;
            if (!engine.beginRemote(key_)) { poisoned_ = true; return; }
            entity_ = ps::Experiment{};
            occupied_ = true;
        }
        entity_.tick(engine, true, true, false, now);
        if (entity_.state() == ps::State::ownership_lost || (entity_.state() == ps::State::failed && entity_.ped())) poisoned_ = true;
    }
private:
    ps::Experiment entity_;
    Intent desired_{};
    Key key_{};
    std::uint64_t consumed_ = 0;
    bool occupied_ = false, poisoned_ = false, busy_ = false;
};
} // namespace grandmp::windows::remote_ped
