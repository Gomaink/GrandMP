#pragma once

#include "grandmp/core/remote_roster.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>

namespace grandmp::core {

// Estado VISUAL portatil, sem dependencias de GTA/RenderWare/Windows/Android.
// O RemoteRoster e a unica fonte de verdade para identidade e sequenciamento.
struct RemotePose {
    std::uint64_t id = 0;
    std::uint32_t sequence = 0;
    float x = 0, y = 0, z = 0;
};

// Adaptadores futuros implementarao essas operacoes NO thread seguro do jogo.
// Os callbacks nao devem bloquear, lancar excecoes nem reter referencias a pose.
class IRemotePlayerSceneSink {
public:
    virtual ~IRemotePlayerSceneSink() = default;
    virtual void spawn(const RemotePose& pose) noexcept = 0;
    virtual void move(const RemotePose& pose) noexcept = 0;
    virtual void despawn(std::uint64_t playerId) noexcept = 0;
    // Transport owners notify a session boundary before synchronizing any poses
    // from the next session. Existing diagnostic sinks need no extra action.
    virtual void resetSession() noexcept {}
};

// Nao cria peds ou mexe na memoria: converte as amostras de rede em comandos
// de apresentacao para um adaptador explicitamente fornecido. Sem extrapolacao.
class RemotePlayerScene {
public:
    using Time = RemoteRoster::Time;
    static constexpr auto interpolationTime = std::chrono::milliseconds(180);
    static constexpr float teleportDistance = 35.0f;

    // Chamar periodicamente no mesmo thread que opera o sink/roster.
    void synchronize(const RemoteRoster& roster, Time now,
                     IRemotePlayerSceneSink& sink);
    // Chamar ANTES de descartar o sink ao desconectar/destruir o cliente.
    void reset(IRemotePlayerSceneSink& sink) noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return entities_.size(); }

private:
    struct Entity {
        RemotePose shown;
        RemotePose origin;
        RemotePose target;
        Time transitionStarted{};
    };
    [[nodiscard]] static RemotePose evaluate(const Entity& entity, Time now) noexcept;
    std::map<std::uint64_t, Entity> entities_;
};

}  // namespace grandmp::core
