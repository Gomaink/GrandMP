#pragma once

#include "grandmp/windows/executable_detection.hpp"

#include <cstdint>

namespace grandmp::windows {

struct PlayerPosition {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

enum class PositionReadStatus {
    integration_unavailable,
    player_absent,
    position_available,
};

struct PositionReading {
    PositionReadStatus status = PositionReadStatus::integration_unavailable;
    PlayerPosition position{};
};

enum class PositionDecision {
    disabled,
    unsupported_game,
    integration_unavailable,
    player_absent,
    invalid_coordinates,
    rate_limited,
    log_position,
};

// Policy only. The Windows game adapter must supply readings from a verified
// game-update callback before any position can be logged.
class PlayerPositionLogGate final {
public:
    explicit PlayerPositionLogGate(GameVersion version, bool enabled) noexcept;

    [[nodiscard]] PositionDecision evaluate(PositionReading reading, std::uint64_t monotonicMilliseconds) noexcept;

private:
    GameVersion version_;
    bool enabled_;
    bool hasLogged_ = false;
    std::uint64_t lastLoggedAt_ = 0;
};

}  // namespace grandmp::windows
