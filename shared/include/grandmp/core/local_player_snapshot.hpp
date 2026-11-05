#pragma once
#include "player_input.hpp"
#include <atomic>
#include <mutex>

namespace grandmp::core {

// One game-thread producer, one UDP consumer. Only a copied value crosses this
// boundary. A session change is a fence, never permission to reuse an old input.
class LocalPlayerSnapshots final {
public:
    enum class Profile { legacy, input_fidelity };
    explicit LocalPlayerSnapshots(Profile profile=Profile::legacy) noexcept : profile_(profile) {}
    std::uint64_t captureIntervalMs() const noexcept {return profile_==Profile::legacy ? 200:100;}
    bool retainFreshOnPublishFailure() const noexcept {return profile_==Profile::input_fidelity;}
    struct Snapshot {
        std::uint64_t session=0, revision=0, capturedMs=0;
        OnFootState state{};
    };
    void session(std::uint64_t value) noexcept { session_.store(value,std::memory_order_release); }
    std::uint64_t session() const noexcept { return session_.load(std::memory_order_acquire); }
    bool publish(Snapshot value) noexcept {
        if (!value.session || value.session!=session() || !value.revision || !validOnFoot(value.state)) return false;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if (!lock || value.session!=session() || value.revision<=latest_.revision) return false;
        latest_=value;return true;
    }
    void invalidate() noexcept { invalidThrough_.store(revision_.load(),std::memory_order_release); }
    std::uint64_t nextRevision() noexcept { // Producer only; never wrap/reuse.
        if (revision_.load()==UINT64_MAX) { invalidate();return 0; }
        return revision_.fetch_add(1)+1;
    }
    // UDP thread only. Legacy remains 5 Hz/one send per revision. Fidelity uses
    // >=120 ms (server floor 100 ms + one 20 ms worker tick), never a catch-up burst.
    // A repeated fresh value gets a NEW wire sequence in sendOnFoot, not a duplicate.
    // Its original capturedMs is never renewed here; no sample survives 200 ms.
    bool take(std::uint64_t now,Snapshot& result) noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if (!lock || !session() || latest_.session!=session() || latest_.revision<consumed_ ||
            (profile_==Profile::legacy && latest_.revision==consumed_) ||
            latest_.revision<=invalidThrough_.load(std::memory_order_acquire) || now<latest_.capturedMs ||
            now-latest_.capturedMs>=200 || (sent_ && (now<lastSend_ || now-lastSend_<(profile_==Profile::legacy ? 200:120)))) return false;
        result=latest_;consumed_=latest_.revision;lastSend_=now;sent_=true;return true;
    }
private:
    const Profile profile_;
    std::mutex mutex_;
    Snapshot latest_{};
    std::atomic<std::uint64_t> session_{0},revision_{0},invalidThrough_{0};
    std::uint64_t consumed_=0,lastSend_=0;
    bool sent_=false;
};
} // namespace grandmp::core
