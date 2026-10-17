#pragma once

#include "grandmp/core/protocol.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace grandmp::core {

// Modelo portatil, usado pelo Windows agora e reutilizavel no Android.
// UDP e best-effort: snapshots periodicos corrigem perda de Join/Position;
// Leave deixa um tombstone para nao ressuscitar participantes antigos.
class RemoteRoster {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    struct Player {
        std::uint64_t id = 0;
        std::uint32_t sequence = 0;
        bool hasPosition = false;
        float x = 0, y = 0, z = 0;
        Time lastSeen{};
    };

    enum class PositionResult { ignored, added, updated, unchanged };

    [[nodiscard]] bool joined(const protocol::RemoteJoin& message, Time now);
    [[nodiscard]] PositionResult position(const protocol::RemotePosition& message, Time now);
    [[nodiscard]] bool left(const protocol::RemoteLeave& message, Time now);
    [[nodiscard]] std::vector<std::uint64_t> expire(Time now);
    void clear() noexcept;
    [[nodiscard]] const Player* get(std::uint64_t playerId) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return players_.size(); }
    template <typename Visitor>
    void forEach(Visitor&& visitor) const {
        for (const auto& [id, player] : players_) {
            (void)id;
            visitor(player);
        }
    }

    static constexpr std::size_t maxPlayers = 63;
    static constexpr auto playerTimeout = std::chrono::seconds(8);
    static constexpr auto tombstoneTimeout = std::chrono::seconds(30);

private:
    std::map<std::uint64_t, Player> players_;
    std::map<std::uint64_t, Time> departed_;
};

}  // namespace grandmp::core
