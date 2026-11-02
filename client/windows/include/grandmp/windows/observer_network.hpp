#pragma once

#include "grandmp/windows/position_observer.hpp"
#include "grandmp/core/remote_roster.hpp"
#include "grandmp/core/remote_player_scene.hpp"
#include "grandmp/core/remote_player_controller.hpp"
#include "grandmp/core/local_player_snapshot.hpp"
#include "grandmp/core/ipv4_endpoint.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace grandmp::windows {

// Legacy Alpha 0.8 endpoint parser: strict loopback, without DNS or LAN opt-in.
[[nodiscard]] bool parseLoopbackEndpoint(std::wstring_view address,
                                         std::uint16_t& port) noexcept;
// Explicit Alpha 0.9 lab path; the legacy parser/start(port) remain loopback.
[[nodiscard]] bool parseLanEndpoint(std::wstring_view address,core::Ipv4Endpoint& endpoint) noexcept;

class ObserverNetwork {
public:
    ObserverNetwork() = default;
    ~ObserverNetwork();
    ObserverNetwork(const ObserverNetwork&) = delete;
    ObserverNetwork& operator=(const ObserverNetwork&) = delete;

    [[nodiscard]] bool start(std::uint16_t port) noexcept;
    [[nodiscard]] bool start(core::Ipv4Endpoint endpoint) noexcept;
    // Enable receive-only scene preview without a local GTA position source.
    // The network phase still only enqueues scene commands into its sink.
    [[nodiscard]] bool beginRemoteScenePreview() noexcept;
    void tick(std::optional<Coordinates> position) noexcept;
    // Real-client UDP worker seam. No GTA access; consume at most one fresh copy.
    [[nodiscard]] bool tickLocal(core::LocalPlayerSnapshots& snapshots) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool connected() const noexcept { return sessionId_ != 0; }
    [[nodiscard]] std::uint64_t sessionId() const noexcept { return sessionId_; }
    [[nodiscard]] std::uint32_t sentSequence() const noexcept { return sequence_; } // UDP owner only.
    // Sink opcional de diagnostico; seu tempo de vida deve superar o do observer.
    // Futuro adapter GTA devera operar apenas em um callback nativo homologado.
    void setRemoteSceneSink(core::IRemotePlayerSceneSink* sink) noexcept;
    // Explicit opt-in only. Controller outlives network, like the existing sink.
    void setRemotePlayerController(core::RemotePlayerController* controller) noexcept;
    // Caller supplies an already captured, validated sample. This never reads GTA.
    // UDP-owner thread only; native capture publishes copied values separately.
    [[nodiscard]] bool sendOnFoot(const core::OnFootState& sample) noexcept;

private:
    void sendHello() noexcept;
    void receivePackets() noexcept;
    void sendSnapshotRequest() noexcept;
    void sendPosition(const Coordinates& position) noexcept;
    void resetRemoteScene(core::RemoteEndReason reason=core::RemoteEndReason::session_changed) noexcept;

    std::uintptr_t socket_ = ~std::uintptr_t{0};
    core::Ipv4Endpoint endpoint_{};
    std::uint64_t nonce_ = 0;
    std::uint64_t sessionId_ = 0;
    std::uint32_t sequence_ = 0;
    std::uint64_t lastHelloMs_ = 0;
    std::uint64_t lastWelcomeMs_ = 0;
    std::uint64_t lastSnapshotMs_ = 0;
    core::RemoteRoster roster_;
    core::RemotePlayerScene scene_;
    core::IRemotePlayerSceneSink* sceneSink_ = nullptr;
    core::RemotePlayerController* playerController_ = nullptr;
    bool winsockStarted_ = false;
    bool initiated_ = false;
};

}  // namespace grandmp::windows
