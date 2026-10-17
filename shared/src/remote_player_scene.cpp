#include "grandmp/core/remote_player_scene.hpp"

#include <algorithm>
#include <cmath>

namespace grandmp::core {
namespace {

[[nodiscard]] bool samePosition(const RemotePose& a, const RemotePose& b) noexcept {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

[[nodiscard]] bool teleport(const RemotePose& a, const RemotePose& b) noexcept {
    const double dx = static_cast<double>(a.x) - b.x;
    const double dy = static_cast<double>(a.y) - b.y;
    const double dz = static_cast<double>(a.z) - b.z;
    return dx * dx + dy * dy + dz * dz >
        static_cast<double>(RemotePlayerScene::teleportDistance) *
        RemotePlayerScene::teleportDistance;
}

}  // namespace

RemotePose RemotePlayerScene::evaluate(const Entity& entity, Time now) noexcept {
    if (now <= entity.transitionStarted) return entity.origin;
    const auto elapsed = now - entity.transitionStarted;
    if (elapsed >= interpolationTime) return entity.target;
    const double t = static_cast<double>(
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count()) /
        static_cast<double>(std::chrono::duration_cast<std::chrono::microseconds>(
            interpolationTime).count());
    auto lerp = [t](float a, float b) {
        return static_cast<float>(static_cast<double>(a) +
            (static_cast<double>(b) - a) * t);
    };
    return RemotePose{entity.target.id, entity.target.sequence,
                      lerp(entity.origin.x, entity.target.x),
                      lerp(entity.origin.y, entity.target.y),
                      lerp(entity.origin.z, entity.target.z)};
}

void RemotePlayerScene::synchronize(const RemoteRoster& roster, Time now,
                                   IRemotePlayerSceneSink& sink) {
    // Nunca criar entidade para um Join sem uma posicao validada.
    roster.forEach([&](const RemoteRoster::Player& player) {
        if (!player.hasPosition) return;
        const RemotePose incoming{player.id, player.sequence, player.x, player.y, player.z};
        auto it = entities_.find(player.id);
        if (it == entities_.end()) {
            Entity entity{incoming, incoming, incoming, now};
            entities_.emplace(player.id, entity);
            sink.spawn(incoming);
            return;
        }
        Entity& entity = it->second;
        // Duplicatas, snapshots repetidos e refresh nao reiniciam a interpolacao.
        if (incoming.sequence != entity.target.sequence) {
            const RemotePose current = evaluate(entity, now);
            entity.origin = current;
            entity.target = incoming;
            entity.transitionStarted = now;
            // Grandes saltos (teleport, respawn) nao atravessam o mapa a lerp.
            if (teleport(current, incoming)) entity.origin = incoming;
        }
        const RemotePose evaluated = evaluate(entity, now);
        if (!samePosition(evaluated, entity.shown) ||
            evaluated.sequence != entity.shown.sequence) {
            entity.shown = evaluated;
            sink.move(evaluated);
        }
    });

    for (auto it = entities_.begin(); it != entities_.end();) {
        const auto* player = roster.get(it->first);
        if (!player || !player->hasPosition) {
            sink.despawn(it->first);
            it = entities_.erase(it);
        } else {
            ++it;
        }
    }
}

void RemotePlayerScene::reset(IRemotePlayerSceneSink& sink) noexcept {
    for (const auto& [id, entity] : entities_) {
        (void)entity;
        sink.despawn(id);
    }
    entities_.clear();
}

}  // namespace grandmp::core
