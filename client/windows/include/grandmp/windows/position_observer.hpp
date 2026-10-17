#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace grandmp::windows {

// PROCESS_VM_READ (0x0010) | PROCESS_QUERY_LIMITED_INFORMATION (0x1000).
// Mantido sem windows.h para que o leitor seja testável em Win32 e x64.
inline constexpr std::uint32_t observerProcessAccess = 0x1010;

struct Coordinates {
    float x = 0;
    float y = 0;
    float z = 0;
};

struct ReadResult {
    bool success = false;
    std::size_t bytesRead = 0;
};

class MemoryReader {
public:
    virtual ~MemoryReader() = default;
    [[nodiscard]] virtual ReadResult read(std::uint32_t address,
                                          std::span<std::byte> destination) noexcept = 0;
};

enum class PositionStatus {
    available,
    player_absent,
    invalid_pointer,
    read_failed,
    partial_read,
    changed_pointer,
    non_finite,
};

struct PositionRead {
    PositionStatus status = PositionStatus::read_failed;
    Coordinates coordinates{};
};

// Endereços e offsets exclusivos do GTA SA PC 1.0 em base 0x00400000.
// O chamador deve homologar o fingerprint Hoodlum e a base ANTES de chamar.
[[nodiscard]] PositionRead readPlayerPosition(MemoryReader& reader) noexcept;

enum class ObserverState {
    game_not_found,
    waiting_for_player,
    player_available,
    temporarily_unavailable,
    process_exited,
};

class PollState {
public:
    [[nodiscard]] ObserverState update(bool processAlive, PositionStatus position) noexcept;
    [[nodiscard]] ObserverState state() const noexcept { return state_; }

private:
    ObserverState state_ = ObserverState::game_not_found;
    bool wasPresent_ = false;
};

}  // namespace grandmp::windows
