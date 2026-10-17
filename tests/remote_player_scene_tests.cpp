#include "grandmp/core/remote_player_scene.hpp"

#include <chrono>
#include <cmath>
#include <cstdint>
#include <map>
#include <string_view>
#include <vector>

namespace {
using grandmp::core::RemotePlayerScene;
using grandmp::core::RemotePose;
using grandmp::core::RemoteRoster;
using namespace grandmp::protocol;
using namespace std::chrono_literals;

struct RecordingSink final : grandmp::core::IRemotePlayerSceneSink {
    std::vector<std::uint64_t> spawns, despawns;
    std::map<std::uint64_t, RemotePose> poses;
    int moves = 0;
    void spawn(const RemotePose& pose) noexcept override {
        spawns.push_back(pose.id);
        poses[pose.id] = pose;
    }
    void move(const RemotePose& pose) noexcept override {
        ++moves;
        poses[pose.id] = pose;
    }
    void despawn(std::uint64_t id) noexcept override {
        despawns.push_back(id);
        poses.erase(id);
    }
};

[[nodiscard]] bool near(float value, float expected) {
    return std::abs(value - expected) < 0.015f;
}

[[nodiscard]] bool lifecycle() {
    RemoteRoster roster;
    RemotePlayerScene scene;
    RecordingSink sink;
    const auto t = RemoteRoster::Time{};
    if (!roster.joined(RemoteJoin{12}, t)) return false;
    scene.synchronize(roster, t, sink);
    if (scene.size() != 0 || !sink.spawns.empty()) return false;
    if (roster.position(RemotePosition{12, 1, 10, 20, 30}, t) !=
        RemoteRoster::PositionResult::updated) return false;
    scene.synchronize(roster, t, sink);
    if (scene.size() != 1 || sink.spawns != std::vector<std::uint64_t>{12} ||
        sink.poses.at(12).x != 10 || sink.moves != 0) return false;
    // Join/Position duplicados nao recriam entidades.
    (void)roster.joined(RemoteJoin{12}, t + 100ms);
    (void)roster.position(RemotePosition{12, 1, 999, 999, 999}, t + 100ms);
    scene.synchronize(roster, t + 100ms, sink);
    if (sink.spawns.size() != 1 || sink.moves != 0) return false;
    if (!roster.left(RemoteLeave{12}, t + 300ms)) return false;
    scene.synchronize(roster, t + 300ms, sink);
    scene.synchronize(roster, t + 400ms, sink);
    return scene.size() == 0 && sink.despawns == std::vector<std::uint64_t>{12};
}

[[nodiscard]] bool interpolation() {
    RemoteRoster roster;
    RemotePlayerScene scene;
    RecordingSink sink;
    const auto t = RemoteRoster::Time{};
    (void)roster.position(RemotePosition{1, 1, 0, 0, 0}, t);
    scene.synchronize(roster, t, sink);
    (void)roster.position(RemotePosition{1, 2, 18, 0, 0}, t + 200ms);
    scene.synchronize(roster, t + 200ms, sink);
    if (!near(sink.poses.at(1).x, 0)) return false;
    scene.synchronize(roster, t + 290ms, sink);
    if (!near(sink.poses.at(1).x, 9)) return false;
    // Snapshot repetido nao altera o destino nem reinicia a interpolacao.
    (void)roster.position(RemotePosition{1, 2, 100, 0, 0}, t + 295ms);
    scene.synchronize(roster, t + 295ms, sink);
    if (!near(sink.poses.at(1).x, 9.5f)) return false;
    scene.synchronize(roster, t + 380ms, sink);
    if (!near(sink.poses.at(1).x, 18)) return false;
    const int oldMoves = sink.moves;
    scene.synchronize(roster, t + 900ms, sink);
    return near(sink.poses.at(1).x, 18) && sink.moves == oldMoves;
}

[[nodiscard]] bool interruptedAndTeleport() {
    RemoteRoster roster;
    RemotePlayerScene scene;
    RecordingSink sink;
    const auto t = RemoteRoster::Time{};
    (void)roster.position(RemotePosition{1, 1, 0, 0, 0}, t);
    scene.synchronize(roster, t, sink);
    (void)roster.position(RemotePosition{1, 2, 20, 0, 0}, t + 200ms);
    scene.synchronize(roster, t + 200ms, sink);
    scene.synchronize(roster, t + 290ms, sink);
    if (!near(sink.poses.at(1).x, 10)) return false;
    // Novo destino e interpolado a partir da posicao atualmente exibida.
    (void)roster.position(RemotePosition{1, 3, 30, 0, 0}, t + 290ms);
    scene.synchronize(roster, t + 290ms, sink);
    scene.synchronize(roster, t + 380ms, sink);
    if (!near(sink.poses.at(1).x, 20)) return false;
    // Teleport/respawn nao faz o personagem cruzar o mapa animado.
    (void)roster.position(RemotePosition{1, 4, 1000, -500, 30}, t + 400ms);
    scene.synchronize(roster, t + 400ms, sink);
    return near(sink.poses.at(1).x, 1000) &&
           near(sink.poses.at(1).y, -500) &&
           near(sink.poses.at(1).z, 30);
}

[[nodiscard]] bool resetAndExpiry() {
    RemoteRoster roster;
    RemotePlayerScene scene;
    RecordingSink sink;
    const auto t = RemoteRoster::Time{};
    for (std::uint64_t id = 1; id <= 63; ++id)
        (void)roster.position(RemotePosition{id, 1, static_cast<float>(id), 0, 0}, t);
    scene.synchronize(roster, t, sink);
    if (scene.size() != 63 || sink.spawns.size() != 63) return false;
    (void)roster.expire(t + RemoteRoster::playerTimeout);
    scene.synchronize(roster, t + RemoteRoster::playerTimeout, sink);
    if (scene.size() != 0 || sink.despawns.size() != 63) return false;
    // Novo mundo apos reconnect; ids antigos nunca permanecem.
    roster.clear();
    (void)roster.position(RemotePosition{1000, 1, 1, 1, 1}, t + 10s);
    scene.synchronize(roster, t + 10s, sink);
    if (scene.size() != 1) return false;
    scene.reset(sink);
    scene.reset(sink);
    return scene.size() == 0 && sink.despawns.size() == 64 && sink.poses.empty();
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view test(argv[1]);
    if (test == "lifecycle") return lifecycle() ? 0 : 1;
    if (test == "interpolation") return interpolation() ? 0 : 1;
    if (test == "teleport") return interruptedAndTeleport() ? 0 : 1;
    if (test == "reset") return resetAndExpiry() ? 0 : 1;
    return 2;
}
