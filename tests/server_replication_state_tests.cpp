#include "grandmp/server/sessions.hpp"

#include <chrono>
#include <cstdint>

using grandmp::server::SessionTable;
using grandmp::server::EventKind;
using namespace grandmp::protocol;

static grandmp::server::Endpoint endpoint(std::uint16_t port) {
    return {{{127, 0, 0, 1}}, port};
}

int main() {
    SessionTable sessions(100);
    const auto t = SessionTable::Time{};
    if (sessions.handle(Hello{10}, endpoint(40000), t).sessionId != 100 ||
        sessions.handle(Hello{20}, endpoint(40001), t).sessionId != 101) return 1;
    if (sessions.peersExcept(100).size() != 1 ||
        sessions.peersExcept(100).front().id != 101 ||
        sessions.peersExcept(100).front().position.has_value()) return 2;
    if (sessions.handle(PlayerPosition{101, 1, 9, 8, 7}, endpoint(40001), t).kind !=
        EventKind::position_accepted) return 3;
    const auto snapshot = sessions.peersExcept(100);
    if (snapshot.size() != 1 || !snapshot[0].position ||
        snapshot[0].position->x != 9 || snapshot[0].position->sequence != 1) return 4;
    if (sessions.handle(SnapshotRequest{101}, endpoint(40000), t).kind !=
        EventKind::rejected_endpoint) return 5;
    if (sessions.handle(SnapshotRequest{999}, endpoint(40001), t).kind !=
        EventKind::rejected_unknown_session) return 6;
    if (sessions.handle(SnapshotRequest{100}, endpoint(40000), t).kind !=
        EventKind::snapshot_requested) return 7;
    if (sessions.handle(SnapshotRequest{100}, endpoint(40000),
                        t + std::chrono::milliseconds(900)).kind != EventKind::rejected_rate)
        return 8;
    if (sessions.handle(SnapshotRequest{100}, endpoint(40000),
                        t + std::chrono::seconds(2)).kind != EventKind::snapshot_requested)
        return 9;
    const auto replaced = sessions.handle(Hello{30}, endpoint(40001), t);
    if (replaced.kind != EventKind::connected || replaced.sessionId != 102 ||
        replaced.departedIds.size() != 1 || replaced.departedIds[0] != 101 ||
        sessions.peersExcept(100)[0].id != 102) return 10;
    if (sessions.handle(PlayerPosition{101, 2, 5, 5, 5}, endpoint(40001), t).kind !=
        EventKind::rejected_unknown_session) return 11;
    const auto expired = sessions.expire(t + std::chrono::seconds(13));
    return expired.size() == 2 && sessions.size() == 0 ? 0 : 12;
}
