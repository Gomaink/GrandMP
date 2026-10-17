#include "grandmp/windows/executable_detection.hpp"

#include <algorithm>
#include <array>

namespace grandmp::windows {
namespace {

[[nodiscard]] bool fits(std::span<const std::byte> bytes, std::size_t offset, std::size_t count) noexcept {
    return offset <= bytes.size() && count <= bytes.size() - offset;
}

[[nodiscard]] std::uint16_t read16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset])) |
           static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1]) << 8);
}

[[nodiscard]] std::uint32_t read32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(read16(bytes, offset)) |
           (static_cast<std::uint32_t>(read16(bytes, offset + 2)) << 16);
}

}  // namespace

ExecutableInspection inspectExecutable(std::span<const std::byte> bytes) noexcept {
    ExecutableInspection result{};
    result.fileSize = bytes.size();
    if (!fits(bytes, 0, 0x40) || read16(bytes, 0) != 0x5A4D) {
        return result;
    }

    const auto peOffset = static_cast<std::size_t>(read32(bytes, 0x3C));
    // 4-byte signature, 20-byte COFF header, and fixed 96-byte PE32 optional header.
    if (!fits(bytes, peOffset, 24) || read32(bytes, peOffset) != 0x00004550) {
        return result;
    }

    const auto machine = read16(bytes, peOffset + 4);
    const auto optionalSize = read16(bytes, peOffset + 20);
    if (optionalSize < 96 || !fits(bytes, peOffset + 24, optionalSize)) {
        return result;
    }

    result.timestamp = read32(bytes, peOffset + 8);
    const auto characteristics = read16(bytes, peOffset + 22);
    result.status = machine == 0x014c && read16(bytes, peOffset + 24) == 0x010b &&
                            (characteristics & 0x0002) != 0
                        ? PeStatus::pe32_i386
                        : PeStatus::unsupported_architecture;
    return result;
}

GameVersion recognizeGameVersion(const ExecutableInspection& inspection,
                                 std::span<const std::byte, 32> sha256) noexcept {
    // GTA SA 1.0 US Hoodlum: 14,383,616 bytes, SHA-256 fornecido para esta revisão.
    // MD5 público correspondente: 170b3a9108687b26da2d8901c6948a18.
    constexpr std::array<std::byte, 32> hoodlumSha256{
        std::byte{0xA5}, std::byte{0x59}, std::byte{0xAA}, std::byte{0x77},
        std::byte{0x2F}, std::byte{0xD1}, std::byte{0x36}, std::byte{0x37},
        std::byte{0x91}, std::byte{0x55}, std::byte{0xEF}, std::byte{0xA7},
        std::byte{0x1F}, std::byte{0x00}, std::byte{0xC4}, std::byte{0x7A},
        std::byte{0xAD}, std::byte{0x34}, std::byte{0xBB}, std::byte{0xFE},
        std::byte{0xAE}, std::byte{0x61}, std::byte{0x96}, std::byte{0xB0},
        std::byte{0xFE}, std::byte{0x10}, std::byte{0x47}, std::byte{0xD0},
        std::byte{0x64}, std::byte{0x5C}, std::byte{0xBD}, std::byte{0x26},
    };
    if (inspection.status == PeStatus::pe32_i386 && inspection.fileSize == 14'383'616 &&
        std::equal(sha256.begin(), sha256.end(), hoodlumSha256.begin())) {
        return GameVersion::gta_sa_10us_hoodlum;
    }
    return GameVersion::unknown;
}

}  // namespace grandmp::windows
