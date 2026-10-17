#include "grandmp/windows/executable_detection.hpp"

#include <array>
#include <cstddef>
#include <cstdio>
#include <cstdint>
#include <string_view>

namespace {

using grandmp::windows::PeStatus;
using grandmp::windows::GameVersion;

void write16(std::array<std::byte, 512>& bytes, std::size_t offset, unsigned value) {
    bytes[offset] = static_cast<std::byte>(value & 0xFFU);
    bytes[offset + 1] = static_cast<std::byte>((value >> 8U) & 0xFFU);
}

void write32(std::array<std::byte, 512>& bytes, std::size_t offset, unsigned value) {
    write16(bytes, offset, value & 0xFFFFU);
    write16(bytes, offset + 2, value >> 16U);
}

std::array<std::byte, 512> makePe(unsigned machine, unsigned magic) {
    std::array<std::byte, 512> bytes{};
    write16(bytes, 0, 0x5A4D);
    write32(bytes, 0x3C, 0x80);
    write32(bytes, 0x80, 0x00004550);
    write16(bytes, 0x84, machine);
    write32(bytes, 0x88, 0x12345678);
    write16(bytes, 0x94, 0xE0);
    write16(bytes, 0x96, 0x0002);
    write16(bytes, 0x98, magic);
    return bytes;
}

bool testPe32() {
    const auto bytes = makePe(0x014C, 0x010B);
    const auto inspected = grandmp::windows::inspectExecutable(bytes);
    return inspected.status == PeStatus::pe32_i386 && inspected.timestamp == 0x12345678 &&
           inspected.fileSize == bytes.size();
}

bool testMalformed() {
    auto bytes = makePe(0x014C, 0x010B);
    bytes[0] = std::byte{};
    if (grandmp::windows::inspectExecutable(bytes).status != PeStatus::invalid) {
        return false;
    }
    bytes = makePe(0x014C, 0x010B);
    write32(bytes, 0x3C, 0xFFFF'FFF0U);
    if (grandmp::windows::inspectExecutable(bytes).status != PeStatus::invalid) {
        return false;
    }
    bytes = makePe(0x014C, 0x010B);
    write16(bytes, 0x94, 1);
    return grandmp::windows::inspectExecutable(bytes).status == PeStatus::invalid;
}

bool testPe32Plus() {
    const auto bytes = makePe(0x8664, 0x020B);
    return grandmp::windows::inspectExecutable(bytes).status == PeStatus::unsupported_architecture;
}

bool testUnknownGame() {
    const auto bytes = makePe(0x014C, 0x010B);
    const auto inspected = grandmp::windows::inspectExecutable(bytes);
    std::array<std::byte, 32> digest{};
    return inspected.status == PeStatus::pe32_i386 &&
           grandmp::windows::recognizeGameVersion(inspected, digest) == GameVersion::unknown;
}

std::array<std::byte, 32> hoodlumDigest() {
    constexpr std::string_view hex =
        "A559AA772FD136379155EFA71F00C47AAD34BBFEAE6196B0FE1047D0645CBD26";
    auto digit = [](char value) -> unsigned {
        return value <= '9' ? static_cast<unsigned>(value - '0')
                            : static_cast<unsigned>(value - 'A' + 10);
    };
    std::array<std::byte, 32> digest{};
    for (std::size_t i = 0; i < digest.size(); ++i) {
        digest[i] = static_cast<std::byte>((digit(hex[i * 2]) << 4U) | digit(hex[i * 2 + 1]));
    }
    return digest;
}

bool testHoodlum() {
    const auto bytes = makePe(0x014C, 0x010B);
    auto inspected = grandmp::windows::inspectExecutable(bytes);
    // O teste exercita o classificador; não contém nem simula o arquivo do GTA.
    inspected.fileSize = 14'383'616;
    const auto digest = hoodlumDigest();
    return grandmp::windows::recognizeGameVersion(inspected, digest) ==
           GameVersion::gta_sa_10us_hoodlum;
}

bool testModified() {
    const auto bytes = makePe(0x014C, 0x010B);
    auto inspected = grandmp::windows::inspectExecutable(bytes);
    inspected.fileSize = 14'383'616;
    auto digest = hoodlumDigest();
    digest[0] ^= std::byte{0x01};
    if (grandmp::windows::recognizeGameVersion(inspected, digest) != GameVersion::unknown) {
        return false;
    }
    digest = hoodlumDigest();
    --inspected.fileSize;
    if (grandmp::windows::recognizeGameVersion(inspected, digest) != GameVersion::unknown) {
        return false;
    }
    inspected.fileSize = 14'383'616;
    inspected.status = PeStatus::unsupported_architecture;
    return grandmp::windows::recognizeGameVersion(inspected, digest) == GameVersion::unknown;
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::fputs("expected one test name\n", stderr);
        return 2;
    }
    const std::string_view name(argv[1]);
    const bool passed = (name == "pe32" && testPe32()) ||
                        (name == "malformed" && testMalformed()) ||
                        (name == "pe32plus" && testPe32Plus()) ||
                        (name == "unknown" && testUnknownGame()) ||
                        (name == "hoodlum" && testHoodlum()) ||
                        (name == "modified" && testModified());
    if (!passed) {
        std::fprintf(stderr, "test failed or unknown: %.*s\n", static_cast<int>(name.size()),
                     name.data());
        return 1;
    }
    return 0;
}
