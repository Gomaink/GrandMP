#include "grandmp/windows/player_position_diagnostics.hpp"

#include <cmath>

namespace grandmp::windows {

PlayerPositionLogGate::PlayerPositionLogGate(GameVersion version, bool enabled) noexcept
    : version_(version), enabled_(enabled) {}

PositionDecision PlayerPositionLogGate::evaluate(PositionReading reading, std::uint64_t monotonicMilliseconds) noexcept {
    if (!enabled_) return PositionDecision::disabled;
    if (version_ != GameVersion::gta_sa_10us_hoodlum) return PositionDecision::unsupported_game;
    if (reading.status == PositionReadStatus::integration_unavailable) return PositionDecision::integration_unavailable;
    if (reading.status == PositionReadStatus::player_absent) return PositionDecision::player_absent;
    if (reading.status != PositionReadStatus::position_available) return PositionDecision::integration_unavailable;

    const auto& p = reading.position;
    if (!std::isfinite(p.x) || !std::isfinite(p.y) || !std::isfinite(p.z)) {
        return PositionDecision::invalid_coordinates;
    }
    if (hasLogged_ && (monotonicMilliseconds < lastLoggedAt_ || monotonicMilliseconds - lastLoggedAt_ < 1000)) {
        return PositionDecision::rate_limited;
    }
    hasLogged_ = true;
    lastLoggedAt_ = monotonicMilliseconds;
    return PositionDecision::log_position;
}

}  // namespace grandmp::windows
