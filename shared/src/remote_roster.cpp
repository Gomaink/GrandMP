#include "grandmp/core/remote_roster.hpp"

namespace grandmp::core {

bool RemoteRoster::joined(const protocol::RemoteJoin& message, Time now) {
    if (message.player_id == 0 || departed_.contains(message.player_id)) return false;
    if (auto it = players_.find(message.player_id); it != players_.end()) {
        it->second.lastSeen = now;
        return false;
    }
    if (players_.size() >= maxPlayers) return false;
    return players_.emplace(message.player_id,
                            Player{.id = message.player_id, .lastSeen = now}).second;
}

RemoteRoster::PositionResult RemoteRoster::position(const protocol::RemotePosition& m, Time now) {
    if (m.player_id == 0 || m.sequence == 0 ||
        !protocol::validCoordinates(m.x, m.y, m.z) || departed_.contains(m.player_id))
        return PositionResult::ignored;
    const auto it = players_.find(m.player_id);
    if (it == players_.end()) {
        if (players_.size() >= maxPlayers) return PositionResult::ignored;
        players_.emplace(m.player_id,
                         Player{m.player_id, m.sequence, true, m.x, m.y, m.z, now});
        return PositionResult::added;
    }
    auto& player = it->second;
    if (player.hasPosition && m.sequence < player.sequence) return PositionResult::ignored;
    const bool equal = player.hasPosition && m.sequence == player.sequence;
    player.lastSeen = now;
    if (equal) return PositionResult::unchanged;
    player.sequence = m.sequence;
    player.hasPosition = true;
    player.x = m.x; player.y = m.y; player.z = m.z;
    return PositionResult::updated;
}

bool RemoteRoster::left(const protocol::RemoteLeave& message, Time now) {
    if (message.player_id == 0) return false;
    // Preservar no maximo 64 tombstones: sessoes removidas nao podem
    // consumir memoria indefinidamente em cliente nao autenticado.
    if (!departed_.contains(message.player_id) && departed_.size() >= 64) {
        auto oldest = departed_.begin();
        for (auto it = departed_.begin(); it != departed_.end(); ++it)
            if (it->second < oldest->second) oldest = it;
        departed_.erase(oldest);
    }
    departed_[message.player_id] = now;
    return players_.erase(message.player_id) != 0;
}

std::vector<std::uint64_t> RemoteRoster::expire(Time now) {
    std::vector<std::uint64_t> removed;
    for (auto it = players_.begin(); it != players_.end();) {
        if (now >= it->second.lastSeen && now - it->second.lastSeen >= playerTimeout) {
            removed.push_back(it->first);
            it = players_.erase(it);
        } else ++it;
    }
    for (auto it = departed_.begin(); it != departed_.end();) {
        if (now >= it->second && now - it->second >= tombstoneTimeout)
            it = departed_.erase(it);
        else ++it;
    }
    return removed;
}

void RemoteRoster::clear() noexcept { players_.clear(); departed_.clear(); }

const RemoteRoster::Player* RemoteRoster::get(std::uint64_t id) const noexcept {
    const auto it = players_.find(id);
    return it == players_.end() ? nullptr : &it->second;
}

}  // namespace grandmp::core
