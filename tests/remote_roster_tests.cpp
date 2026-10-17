#include "grandmp/core/remote_roster.hpp"

#include <chrono>
#include <cstdint>

namespace {
using grandmp::core::RemoteRoster;
using namespace grandmp::protocol;

bool rosterAndLoss() {
    RemoteRoster roster;
    const auto start = RemoteRoster::Time{};
    if (!roster.joined(RemoteJoin{200}, start) || roster.joined(RemoteJoin{200}, start) ||
        roster.size() != 1 || !roster.get(200)) return false;
    if (roster.position(RemotePosition{200, 1, 1, 2, 3}, start) !=
        RemoteRoster::PositionResult::updated) return false;
    if (roster.position(RemotePosition{200, 1, 4, 5, 6}, start) !=
        RemoteRoster::PositionResult::unchanged || roster.get(200)->x != 1) return false;
    if (roster.position(RemotePosition{200, 0, 0, 0, 0}, start) !=
        RemoteRoster::PositionResult::ignored) return false;
    if (roster.position(RemotePosition{200, 2, 4, 5, 6}, start) !=
        RemoteRoster::PositionResult::updated || roster.get(200)->x != 4) return false;
    if (roster.position(RemotePosition{201, 6, 7, 8, 9}, start) !=
        RemoteRoster::PositionResult::added) return false;
    if (!roster.left(RemoteLeave{200}, start) || roster.size() != 1) return false;
    // Pacotes atrasados nao recriam um jogador ja removido.
    if (roster.joined(RemoteJoin{200}, start + std::chrono::seconds(1)) ||
        roster.position(RemotePosition{200, 3, 8, 8, 8}, start) !=
        RemoteRoster::PositionResult::ignored) return false;
    // Perda de leave recuperada por timeout.
    auto expired = roster.expire(start + RemoteRoster::playerTimeout);
    if (expired.size() != 1 || expired[0] != 201 || roster.size() != 0) return false;
    (void)roster.expire(start + RemoteRoster::tombstoneTimeout);
    return roster.joined(RemoteJoin{200}, start + RemoteRoster::tombstoneTimeout);
}

bool boundsAndReset() {
    RemoteRoster roster;
    const auto time = RemoteRoster::Time{};
    for (std::uint64_t i = 1; i <= 63; ++i)
        if (!roster.joined(RemoteJoin{i}, time)) return false;
    if (roster.joined(RemoteJoin{64}, time) || roster.size() != 63) return false;
    roster.clear();
    return roster.size() == 0 && roster.get(1) == nullptr &&
           roster.position(RemotePosition{64, 1, 1, 2, 3}, time) ==
               RemoteRoster::PositionResult::added;
}
}  // namespace

int main() { return rosterAndLoss() && boundsAndReset() ? 0 : 1; }
