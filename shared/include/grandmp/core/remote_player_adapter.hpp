#pragma once

#include "grandmp/core/remote_player_scene.hpp"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <mutex>
#include <set>
#include <thread>

namespace grandmp::core {

// Backend boundary for a platform-owned entity representation. Calls occur only
// from QueuedRemotePlayerAdapter::pump(), on its single owner thread.
class IRemotePlayerEntityBackend {
public:
    virtual ~IRemotePlayerEntityBackend() = default;
    [[nodiscard]] virtual bool available() const noexcept = 0;
    // A failed create leaves no partial entity; repeated create by ID is idempotent.
    [[nodiscard]] virtual bool create(const RemotePose& pose) noexcept = 0;
    // update applies an absolute pose and can safely be retried.
    [[nodiscard]] virtual bool update(const RemotePose& pose) noexcept = 0;
    // Removal is idempotent; true confirms the representation is absent.
    [[nodiscard]] virtual bool remove(std::uint64_t playerId) noexcept = 0;
    // true confirms that the backend has no remaining representations.
    [[nodiscard]] virtual bool removeAll() noexcept = 0;
};

enum class AdapterState { cold, ready, stopping, cleanup_failed, stopped };
enum class NativeCommandKind { create, update, remove, remove_all };

struct NativeCommand {
    NativeCommandKind kind = NativeCommandKind::remove_all;
    RemotePose pose{};
    std::uint64_t playerId = 0;
    std::uint64_t generation = 0;
};

// Bounded value-command bridge. It never invokes an entity backend from the
// producer/sink methods; only an explicitly chosen owner thread can call pump.
// The first pump caller becomes the owner. Integrations must not call pump until
// a safe platform/game execution context has been independently established.
class QueuedRemotePlayerAdapter final : public IRemotePlayerSceneSink {
public:
    static constexpr std::size_t queueCapacity = 256;
    static constexpr std::size_t maxMaterializedPlayers = RemoteRoster::maxPlayers;
    // A persistently failing entity operation must not hold later cleanup behind it.
    static constexpr std::size_t maxConsecutiveBackendFailures = 3;

    explicit QueuedRemotePlayerAdapter(IRemotePlayerEntityBackend& backend) noexcept;
    ~QueuedRemotePlayerAdapter() override;
    QueuedRemotePlayerAdapter(const QueuedRemotePlayerAdapter&) = delete;
    QueuedRemotePlayerAdapter& operator=(const QueuedRemotePlayerAdapter&) = delete;

    [[nodiscard]] bool initialize() noexcept;
    // Binds backend consumption to the calling host thread before network
    // production starts. pump() also claims the first caller for compatibility.
    [[nodiscard]] bool bindOwnerThread() noexcept;
    [[nodiscard]] AdapterState state() const noexcept;
    [[nodiscard]] bool isAvailable() const noexcept;
    [[nodiscard]] std::size_t pending() const noexcept;
    [[nodiscard]] std::size_t droppedCommands() const noexcept;

    void spawn(const RemotePose& pose) noexcept override;
    void move(const RemotePose& pose) noexcept override;
    void despawn(std::uint64_t playerId) noexcept override;
    void resetSession() noexcept override;

    // Establishes an ordered session boundary and requests backend cleanup.
    void resetAll() noexcept;
    // Closes producers promptly. An in-flight backend call completes without
    // the queue lock; cleanup is then performed by pump on the owner thread.
    void shutdown() noexcept;
    // Explicitly retries a cleanup that exhausted its bounded automatic attempts.
    [[nodiscard]] bool retryCleanup() noexcept;
    [[nodiscard]] std::size_t pump(std::size_t budget = 64) noexcept;

private:
    [[nodiscard]] bool enqueue(NativeCommand command) noexcept;
    [[nodiscard]] std::deque<NativeCommand>::iterator lastFor(std::uint64_t id) noexcept;
    void failClosed() noexcept;

    // backend must outlive this adapter. No queue/registry lock is held while
    // calling it; pumping_ prevents nested owner-thread pump calls.
    IRemotePlayerEntityBackend& backend_;
    mutable std::mutex mutex_;
    std::deque<NativeCommand> commands_;
    std::set<std::uint64_t> materialized_;
    std::map<std::uint64_t, std::uint32_t> lastSequence_;
    AdapterState state_ = AdapterState::cold;
    std::thread::id ownerThread_{};
    std::uint64_t generation_ = 1;
    std::size_t droppedCommands_ = 0;
    bool backendAvailable_ = false;
    bool pumping_ = false;
    bool hasInFlightCommand_ = false;
    std::size_t consecutiveBackendFailures_ = 0;
    std::size_t consecutiveCleanupFailures_ = 0;
    std::uint64_t inFlightPlayerId_ = 0;
    NativeCommandKind inFlightKind_ = NativeCommandKind::remove_all;
};

}  // namespace grandmp::core
