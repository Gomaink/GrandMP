#include "grandmp/windows/player_position_diagnostics.hpp"

#include <cstdio>
#include <limits>
#include <string_view>

namespace {

using grandmp::windows::GameVersion;
using grandmp::windows::PlayerPosition;
using grandmp::windows::PlayerPositionLogGate;
using grandmp::windows::PositionDecision;
using grandmp::windows::PositionReading;
using grandmp::windows::PositionReadStatus;

constexpr PositionReading validPosition{PositionReadStatus::position_available, PlayerPosition{1.0f, -2.0f, 3.0f}};

bool unknownGame() {
    PlayerPositionLogGate gate(GameVersion::unknown, true);
    return gate.evaluate(validPosition, 1000) == PositionDecision::unsupported_game;
}

bool playerAbsent() {
    PlayerPositionLogGate gate(GameVersion::gta_sa_10us_hoodlum, true);
    return gate.evaluate({PositionReadStatus::player_absent, {}}, 1000) == PositionDecision::player_absent &&
           gate.evaluate(validPosition, 1000) == PositionDecision::log_position;
}

bool coordinates() {
    PlayerPositionLogGate gate(GameVersion::gta_sa_10us_hoodlum, true);
    const auto infinity = std::numeric_limits<float>::infinity();
    const auto nan = std::numeric_limits<float>::quiet_NaN();
    return gate.evaluate({PositionReadStatus::position_available, {infinity, 0.0f, 0.0f}}, 1000) == PositionDecision::invalid_coordinates &&
           gate.evaluate({PositionReadStatus::position_available, {0.0f, nan, 0.0f}}, 1000) == PositionDecision::invalid_coordinates &&
           gate.evaluate({PositionReadStatus::position_available, {0.0f, 0.0f, -infinity}}, 1000) == PositionDecision::invalid_coordinates &&
           gate.evaluate(validPosition, 1000) == PositionDecision::log_position;
}

bool frequency() {
    PlayerPositionLogGate gate(GameVersion::gta_sa_10us_hoodlum, true);
    return gate.evaluate(validPosition, 5000) == PositionDecision::log_position &&
           gate.evaluate(validPosition, 5999) == PositionDecision::rate_limited &&
           gate.evaluate(validPosition, 6000) == PositionDecision::log_position &&
           gate.evaluate(validPosition, 5900) == PositionDecision::rate_limited &&
           gate.evaluate(validPosition, 7000) == PositionDecision::log_position;
}

bool unavailable() {
    PlayerPositionLogGate gate(GameVersion::gta_sa_10us_hoodlum, true);
    PlayerPositionLogGate disabled(GameVersion::gta_sa_10us_hoodlum, false);
    return gate.evaluate({}, 1000) == PositionDecision::integration_unavailable &&
           disabled.evaluate(validPosition, 1000) == PositionDecision::disabled &&
           gate.evaluate(validPosition, 1000) == PositionDecision::log_position;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) return 2;
    const std::string_view name(argv[1]);
    const bool passed = (name == "unknown" && unknownGame()) ||
                        (name == "absent" && playerAbsent()) ||
                        (name == "coordinates" && coordinates()) ||
                        (name == "frequency" && frequency()) ||
                        (name == "unavailable" && unavailable());
    if (!passed) {
        std::fprintf(stderr, "position test failed: %.*s\n", static_cast<int>(name.size()), name.data());
        return 1;
    }
    return 0;
}
