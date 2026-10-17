#pragma once

#include "grandmp/windows/position_observer.hpp"
#include "grandmp/core/remote_roster.hpp"
#include "grandmp/core/remote_player_scene.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace grandmp::windows {

// O modo online experimental aceita apenas IPv4 loopback. Não resolve nomes nem
// abre acesso a redes externas neste slice.
[[nodiscard]] bool parseLoopbackEndpoint(std::wstring_view address,
                                         std::uint16_t& port) noexcept;

class ObserverNetwork {
public:
    ObserverNetwork() = default;
    ~ObserverNetwork();
    ObserverNetwork(const ObserverNetwork&) = delete;
    ObserverNetwork& operator=(const ObserverNetwork&) = delete;

    [[nodiscard]] bool start(std::uint16_t port) noexcept;
    // Enable receive-only scene preview without a local GTA position source.
    // The network phase still only enqueues scene commands into its sink.
    [[nodiscard]] bool beginRemoteScenePreview() noexcept;
    void tick(std::optional<Coordinates> position) noexcept;
    void stop() noexcept;
    [[nodiscard]] bool connected() const noexcept { return sessionId_ != 0; }
    [[nodiscard]] std::uint64_t sessionId() const noexcept { return sessionId_; }
    // Sink opcional de diagnostico; seu tempo de vida deve superar o do observer.
    // Futuro adapter GTA devera operar apenas em um callback nativo homologado.
    void setRemoteSceneSink(core::IRemotePlayerSceneSink* sink) noexcept;

private:
    void sendHello() noexcept;
    void receivePackets() noexcept;
    void sendSnapshotRequest() noexcept;
    void sendPosition(const Coordinates& position) noexcept;
    void resetRemoteScene() noexcept;

    std::uintptr_t socket_ = ~std::uintptr_t{0};
    std::uint16_t port_ = 0;
    std::uint64_t nonce_ = 0;
    std::uint64_t sessionId_ = 0;
    std::uint32_t sequence_ = 0;
    std::uint64_t lastHelloMs_ = 0;
    std::uint64_t lastWelcomeMs_ = 0;
    std::uint64_t lastSnapshotMs_ = 0;
    core::RemoteRoster roster_;
    core::RemotePlayerScene scene_;
    core::IRemotePlayerSceneSink* sceneSink_ = nullptr;
    bool winsockStarted_ = false;
    bool initiated_ = false;
};

}  // namespace grandmp::windows
