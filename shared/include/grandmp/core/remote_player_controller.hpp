#pragma once

#include "grandmp/core/remote_player_scene.hpp"
#include <array>
#include <mutex>

namespace grandmp::core {

struct RemotePlayerKey {
    std::uint64_t player = 0, generation = 0;
    bool operator==(const RemotePlayerKey&) const = default;
};
enum class ControlFreshness { awaiting_controls, fresh, input_timeout };
// Evidence available at this receiver. RemoteLeave v1 does not encode whether
// the server saw Disconnect or expired the sender: never guess that distinction.
enum class RemoteEndReason { roster_absent, server_leave, roster_timeout, session_timeout,
    session_changed, network_stopped, controller_error, controller_detached };
inline const char* remoteEndName(RemoteEndReason reason) noexcept {
    switch (reason) {
    case RemoteEndReason::server_leave: return "server_remote_leave_cause_unspecified";
    case RemoteEndReason::roster_timeout: return "receiver_roster_timeout";
    case RemoteEndReason::session_timeout: return "receiver_session_timeout";
    case RemoteEndReason::session_changed: return "receiver_session_changed";
    case RemoteEndReason::network_stopped: return "receiver_stopped";
    case RemoteEndReason::controller_error: return "controller_error";
    case RemoteEndReason::controller_detached: return "controller_detached";
    default: return "entity_absent_from_roster";
    }
}
struct RemotePlayerEnd { RemotePlayerKey key{}; RemoteEndReason reason=RemoteEndReason::roster_absent; };
struct RemotePlayerFrame {
    RemotePlayerKey key;
    std::uint32_t sequence = 0;
    OnFootState reference{}; // Reconciliation reference ONLY, never an applyPose command.
    RemotePose presentation{}; // Existing Scene interpolation, kept separate from controls.
    ControlFreshness freshness = ControlFreshness::awaiting_controls;
};
struct RemotePlayerFrames {
    std::uint64_t recipientSession = 0;
    std::size_t count = 0;
    std::array<RemotePlayerFrame,RemoteRoster::maxPlayers> players{};
    std::size_t endedCount=0;
    std::array<RemotePlayerEnd,RemoteRoster::maxPlayers> ended{};
};

// Network/Scene producer. Consumer receives COPIES via nonblocking tryRead;
// nothing here owns a PED, CPad, task, native pointer or entity backend.
// The legacy absolute-pose adapter is intentionally NOT used for CPlayerPed.
class RemotePlayerController final : private IRemotePlayerSceneSink {
public:
    using Time = RemoteRoster::Time;
    static constexpr auto inputTimeout = onFootInputTimeout;
    // Producer thread only; ends the old Scene and invalidates all generations.
    void beginSession(std::uint64_t recipient, RemoteEndReason reason=RemoteEndReason::session_changed) noexcept;
    // Producer only, after the roster accepted removal/timeout. No engine calls.
    void endRemote(std::uint64_t id,RemoteEndReason reason) noexcept;
    [[nodiscard]] bool receive(const protocol::RemoteOnFoot& sample,
                               const RemoteRoster& roster, Time now) noexcept;
    void synchronize(const RemoteRoster& roster, Time now);
    // Snapshot is NOT an ownership reservation. A future native binding must
    // revalidate entity/context and preserve/restore the complete local context.
    [[nodiscard]] bool tryRead(Time now, RemotePlayerFrames& out) const noexcept;
private:
    struct Entry {
        RemotePlayerFrame frame{};
        Time received{};
        bool visible = false;
    };
    void spawn(const RemotePose& pose) noexcept override;
    void move(const RemotePose& pose) noexcept override;
    void despawn(std::uint64_t id) noexcept override;
    Entry* find(std::uint64_t id) noexcept;
    Entry* admit(std::uint64_t id) noexcept;
    void rememberEnd(Entry&,RemoteEndReason) noexcept; // mutex held

    RemotePlayerScene scene_;
    mutable std::mutex mutex_;
    std::array<Entry,RemoteRoster::maxPlayers> entries_{};
    std::uint64_t recipient_ = 0, nextGeneration_ = 0;
    std::array<RemotePlayerEnd,RemoteRoster::maxPlayers> ended_{};
    std::size_t endedCount_=0,endedCursor_=0;
};

} // namespace grandmp::core
