#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace grandmp::windows {

enum class PeStatus {
    invalid,
    unsupported_architecture,
    pe32_i386,
};

enum class GameVersion {
    unknown,
    gta_sa_10us_hoodlum,
};

struct ExecutableInspection {
    PeStatus status = PeStatus::invalid;
    std::uint32_t timestamp = 0;
    std::uint64_t fileSize = 0;
};

// Inspeciona somente o formato PE do arquivo. PE32/i386 não identifica a versão do GTA.
[[nodiscard]] ExecutableInspection inspectExecutable(std::span<const std::byte> bytes) noexcept;
[[nodiscard]] GameVersion recognizeGameVersion(const ExecutableInspection& inspection,
                                               std::span<const std::byte, 32> sha256) noexcept;

}  // namespace grandmp::windows
