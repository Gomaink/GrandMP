#include "grandmp/windows/position_observer.hpp"

#include <array>
#include <cmath>
#include <cstring>
#include <limits>

namespace grandmp::windows {
namespace {

// plugin-sdk 15f15b60: CWorld::Players é array inline em 0xB7CD98;
// CPlayerInfo::m_pPed +0; CPlaceable::m_matrix +0x14,
// CMatrix::pos +0x30 e m_placement.m_vPosn +0x04.
constexpr std::uint32_t playerPedAddress = 0x00B7CD98;
constexpr std::uint32_t placementOffset = 0x04;
constexpr std::uint32_t matrixPointerOffset = 0x14;
constexpr std::uint32_t matrixPositionOffset = 0x30;

[[nodiscard]] bool plausiblePointer(std::uint32_t pointer) noexcept {
    return pointer >= 0x10000 && (pointer & 3U) == 0;
}

[[nodiscard]] bool canAdd(std::uint32_t pointer, std::uint32_t offset) noexcept {
    return pointer <= std::numeric_limits<std::uint32_t>::max() - offset;
}

template <typename T>
[[nodiscard]] PositionStatus readValue(MemoryReader& reader, std::uint32_t address,
                                       T& value) noexcept {
    std::array<std::byte, sizeof(T)> bytes{};
    const auto result = reader.read(address, bytes);
    if (!result.success && result.bytesRead == 0) return PositionStatus::read_failed;
    if (result.bytesRead != bytes.size()) return PositionStatus::partial_read;
    if (!result.success) return PositionStatus::read_failed;
    std::memcpy(&value, bytes.data(), bytes.size());
    return PositionStatus::available;
}

}  // namespace

PositionRead readPlayerPosition(MemoryReader& reader) noexcept {
    std::uint32_t ped = 0;
    auto status = readValue(reader, playerPedAddress, ped);
    if (status != PositionStatus::available) return {status, {}};
    if (ped == 0) return {PositionStatus::player_absent, {}};
    if (!plausiblePointer(ped) || !canAdd(ped, matrixPointerOffset)) {
        return {PositionStatus::invalid_pointer, {}};
    }

    std::uint32_t matrix = 0;
    status = readValue(reader, ped + matrixPointerOffset, matrix);
    if (status != PositionStatus::available) return {status, {}};

    std::uint32_t positionAddress = 0;
    if (matrix != 0) {
        if (!plausiblePointer(matrix) || !canAdd(matrix, matrixPositionOffset)) {
            return {PositionStatus::invalid_pointer, {}};
        }
        positionAddress = matrix + matrixPositionOffset;
    } else {
        if (!canAdd(ped, placementOffset)) return {PositionStatus::invalid_pointer, {}};
        positionAddress = ped + placementOffset;
    }

    Coordinates coordinates{};
    static_assert(sizeof(Coordinates) == 12);
    status = readValue(reader, positionAddress, coordinates);
    if (status != PositionStatus::available) return {status, {}};

    // Um objeto removido ou substituído durante o polling invalida a amostra.
    std::uint32_t pedAfter = 0;
    status = readValue(reader, playerPedAddress, pedAfter);
    if (status != PositionStatus::available) return {status, {}};
    std::uint32_t matrixAfter = 0;
    status = readValue(reader, ped + matrixPointerOffset, matrixAfter);
    if (status != PositionStatus::available) return {status, {}};
    if (pedAfter != ped || matrixAfter != matrix) return {PositionStatus::changed_pointer, {}};

    if (!std::isfinite(coordinates.x) || !std::isfinite(coordinates.y) ||
        !std::isfinite(coordinates.z)) {
        return {PositionStatus::non_finite, {}};
    }
    return {PositionStatus::available, coordinates};
}

ObserverState PollState::update(bool processAlive, PositionStatus position) noexcept {
    if (!processAlive) {
        state_ = wasPresent_ ? ObserverState::process_exited : ObserverState::game_not_found;
        return state_;
    }
    wasPresent_ = true;
    if (position == PositionStatus::available) state_ = ObserverState::player_available;
    else if (position == PositionStatus::player_absent) state_ = ObserverState::waiting_for_player;
    else state_ = ObserverState::temporarily_unavailable;
    return state_;
}

}  // namespace grandmp::windows
