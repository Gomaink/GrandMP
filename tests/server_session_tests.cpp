#include "grandmp/server/sessions.hpp"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string_view>

namespace {

using namespace grandmp;
using server::EventKind;
using server::SessionTable;

server::Endpoint endpoint(std::uint16_t port) { return {{{127, 0, 0, 1}}, port}; }

bool sessionsAndSequence() {
    SessionTable table(100);
    const auto start = SessionTable::Time{};
    const auto first = table.handle(protocol::Hello{11}, endpoint(40001), start);
    const auto second = table.handle(protocol::Hello{22}, endpoint(40002), start);
    if (first.kind != EventKind::connected || first.sessionId != 100 ||
        second.kind != EventKind::connected || second.sessionId != 101 || table.size() != 2 ||
        !first.response || !second.response) return false;
    const auto repeat = table.handle(protocol::Hello{11}, endpoint(40001), start);
    if (repeat.kind != EventKind::welcome_repeated || repeat.sessionId != 100 ||
        table.size() != 2) return false;
    const auto p1 = table.handle(protocol::PlayerPosition{100, 1, 1, 2, 3}, endpoint(40001), start);
    const auto p2 = table.handle(protocol::PlayerPosition{101, 1, 4, 5, 6}, endpoint(40002), start);
    if (p1.kind != EventKind::position_accepted || p2.kind != EventKind::position_accepted ||
        !p1.position || !p2.position || p1.position->x != 1 || p2.position->x != 4) return false;
    if (table.handle(protocol::PlayerPosition{100, 1, 1, 2, 3}, endpoint(40001),
                     start + std::chrono::milliseconds(200)).kind !=
        EventKind::rejected_sequence) return false;
    if (table.handle(protocol::PlayerPosition{100, 2, 1, 2, 3}, endpoint(40001),
                     start + std::chrono::milliseconds(50)).kind != EventKind::rejected_rate)
        return false;
    if (table.handle(protocol::PlayerPosition{100, 2, 7, 8, 9}, endpoint(40001),
                     start + std::chrono::milliseconds(200)).kind !=
        EventKind::position_accepted) return false;
    if (table.handle(protocol::PlayerPosition{100, 3, 0, 0, 0}, endpoint(40002),
                     start + std::chrono::milliseconds(400)).kind !=
        EventKind::rejected_endpoint) return false;
    return table.handle(protocol::PlayerPosition{999, 1, 0, 0, 0}, endpoint(40001),
                        start).kind == EventKind::rejected_unknown_session;
}

bool timeoutAndDisconnect() {
    SessionTable table(1);
    const auto start = SessionTable::Time{};
    (void)table.handle(protocol::Hello{1}, endpoint(40001), start);
    (void)table.handle(protocol::Hello{2}, endpoint(40002), start);
    (void)table.handle(protocol::PlayerPosition{1, 1, 1, 2, 3}, endpoint(40001),
                 start + std::chrono::seconds(5));
    const auto expired = table.expire(start + std::chrono::seconds(10));
    if (expired.size() != 1 || expired.front() != 2 || table.size() != 1) return false;
    if (table.handle(protocol::Disconnect{1}, endpoint(40002), start).kind !=
        EventKind::rejected_endpoint) return false;
    if (table.handle(protocol::Disconnect{1}, endpoint(40001), start).kind !=
        EventKind::disconnected || table.size() != 0) return false;
    return table.handle(protocol::Disconnect{1}, endpoint(40001), start).kind ==
           EventKind::rejected_unknown_session;
}

bool restartAndCapacity() {
    const auto start = SessionTable::Time{};
    SessionTable before(100);
    (void)before.handle(protocol::Hello{123}, endpoint(40001), start);
    SessionTable after(500);
    if (after.handle(protocol::PlayerPosition{100, 1, 1, 2, 3}, endpoint(40001), start).kind !=
        EventKind::rejected_unknown_session) return false;
    const auto reconnected = after.handle(protocol::Hello{123}, endpoint(40001), start);
    if (reconnected.kind != EventKind::connected || reconnected.sessionId != 500) return false;
    for (std::uint16_t i = 0; i < SessionTable::maxSessions - 1; ++i) {
        if (after.handle(protocol::Hello{static_cast<std::uint64_t>(i) + 1000},
                         endpoint(static_cast<std::uint16_t>(41000 + i)), start).kind !=
            EventKind::connected) return false;
    }
    return after.size() == SessionTable::maxSessions &&
           after.handle(protocol::Hello{9999}, endpoint(49999), start).kind ==
               EventKind::rejected_capacity;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc != 2) return 2;
    const std::string_view mode = argv[1];
    const bool ok = (mode == "sessions" && sessionsAndSequence()) ||
                    (mode == "timeout" && timeoutAndDisconnect()) ||
                    (mode == "restart" && restartAndCapacity());
    if (!ok) std::fprintf(stderr, "server session test failed: %.*s\n",
                          static_cast<int>(mode.size()), mode.data());
    return ok ? 0 : 1;
}
