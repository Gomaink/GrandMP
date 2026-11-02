#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace grandmp::windows::alpha09_memory_reader {

inline constexpr std::uint32_t imageBase = 0x00400000;
inline constexpr std::uint32_t firstPage = 0x0053B000;
inline constexpr std::uint32_t secondPage = 0x0053C000;
inline constexpr std::uint32_t rangeAddress = 0x0053BEE0;
inline constexpr std::size_t rangeSize = 380;
inline constexpr std::size_t firstPageBytes = secondPage - rangeAddress;
inline constexpr std::size_t secondPageBytes = rangeSize - firstPageBytes;
inline constexpr std::uint32_t expectedMachine = 0x014C; // IMAGE_FILE_MACHINE_I386
inline constexpr std::uint16_t expectedOptionalMagic = 0x010B; // PE32
inline constexpr std::uint32_t expectedTextCharacteristics = 0x60000020;

// Win32 process access constants used for the external target handle.
// Keep this exact mask testable without including Windows headers in the core.
inline constexpr std::uint32_t processQueryInformationRight = 0x00000400;
inline constexpr std::uint32_t processVmReadRight = 0x00000010;
inline constexpr std::uint32_t synchronizeRight = 0x00100000;
inline constexpr std::uint32_t processVmWriteRight = 0x00000020;
inline constexpr std::uint32_t processVmOperationRight = 0x00000008;
inline constexpr std::uint32_t processCreateThreadRight = 0x00000002;
inline constexpr std::uint32_t processTerminateRight = 0x00000001;
inline constexpr std::uint32_t processSuspendResumeRight = 0x00000800;
inline constexpr std::uint32_t readOnlyProcessRights = processQueryInformationRight |
    processVmReadRight | synchronizeRight;
inline constexpr std::uint32_t prohibitedProcessRights = processVmWriteRight |
    processVmOperationRight | processCreateThreadRight | processTerminateRight |
    processSuspendResumeRight;

static_assert((readOnlyProcessRights & prohibitedProcessRights) == 0);

static_assert(firstPageBytes == 288 && secondPageBytes == 92);

struct PeRange {
    std::uint32_t rva = 0;
    std::uint32_t rawOffset = 0;
    std::uint32_t sectionVirtualAddress = 0;
    std::uint32_t sectionRawSize = 0;
    std::uint32_t sectionCharacteristics = 0;
    std::array<char, 9> sectionName{};
};

struct ByteComparison {
    bool complete = false;
    bool equal = false;
    std::uint32_t requested = static_cast<std::uint32_t>(rangeSize);
    std::uint32_t transferred = 0;
    std::uint32_t firstMismatch = 0;
};

struct WorkingSetBits {
    bool valid = false;
    bool shared = false;
    std::uint8_t shareCount = 0;
    std::uint16_t win32Protection = 0;
};

inline std::uint16_t u16(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset])) |
        static_cast<std::uint16_t>(std::to_integer<unsigned char>(bytes[offset + 1]) << 8);
}

inline std::uint32_t u32(std::span<const std::byte> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset])) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 1])) << 8) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 2])) << 16) |
        (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[offset + 3])) << 24);
}

inline bool mapPe32Range(std::span<const std::byte> file, PeRange& result) noexcept {
    constexpr std::size_t dosLfanewOffset = 0x3C;
    constexpr std::size_t fileHeaderOffset = 4;
    constexpr std::size_t optionalHeaderOffset = 24;
    if (file.size() < 64 || u16(file, 0) != 0x5A4D) return false;
    const auto peOffset = u32(file, dosLfanewOffset);
    if (peOffset > file.size() || file.size() - peOffset < 24) return false;
    if (u32(file, peOffset) != 0x00004550) return false;
    const auto header = static_cast<std::size_t>(peOffset) + fileHeaderOffset;
    const auto machine = u16(file, header);
    const auto sectionCount = u16(file, header + 2);
    const auto optionalSize = u16(file, header + 16);
    if (machine != expectedMachine || !sectionCount || sectionCount > 32) return false;
    const auto optional = static_cast<std::size_t>(peOffset) + optionalHeaderOffset;
    if (optional > file.size() || optionalSize > file.size() - optional || optionalSize < 32) return false;
    if (u16(file, optional) != expectedOptionalMagic || u32(file, optional + 28) != imageBase) return false;
    const auto sectionTable = optional + optionalSize;
    if (sectionTable > file.size() || std::size_t(sectionCount) > (file.size() - sectionTable) / 40) return false;
    const auto rva = rangeAddress - imageBase;
    const auto endRva = std::uint64_t{rva} + rangeSize;
    for (std::uint16_t i = 0; i < sectionCount; ++i) {
        const auto section = sectionTable + std::size_t{i} * 40;
        const auto virtualSize = u32(file, section + 8);
        const auto virtualAddress = u32(file, section + 12);
        const auto rawSize = u32(file, section + 16);
        const auto rawOffset = u32(file, section + 20);
        const auto characteristics = u32(file, section + 36);
        const auto sectionEnd = std::uint64_t{virtualAddress} + rawSize;
        if (rva < virtualAddress || endRva > sectionEnd) continue;
        const auto delta = std::uint64_t{rva} - virtualAddress;
        if (delta + rangeSize > virtualSize || delta + rangeSize > rawSize) return false;
        const auto mappedOffset = std::uint64_t{rawOffset} + delta;
        if (mappedOffset > file.size() || rangeSize > file.size() - mappedOffset) return false;
        if (!(characteristics & 0x20000000) || !(characteristics & 0x40000000) ||
            (characteristics & 0x80000000) || virtualSize < rangeSize) return false;
        PeRange candidate{};
        candidate.rva = rva;
        candidate.rawOffset = static_cast<std::uint32_t>(mappedOffset);
        candidate.sectionVirtualAddress = virtualAddress;
        candidate.sectionRawSize = rawSize;
        candidate.sectionCharacteristics = characteristics;
        for (std::size_t n = 0; n < 8; ++n) {
            const auto ch = std::to_integer<unsigned char>(file[section + n]);
            candidate.sectionName[n] = ch ? static_cast<char>(ch) : '\0';
        }
        if (candidate.sectionName != std::array<char, 9>{'.','t','e','x','t','\0','\0','\0','\0'} ||
            characteristics != expectedTextCharacteristics) return false;
        result = candidate;
        return true;
    }
    return false;
}

inline ByteComparison compareBytes(std::span<const std::byte> expected,
                                   std::span<const std::byte> observed,
                                   std::size_t transferred, bool readSucceeded) noexcept {
    ByteComparison result{};
    result.transferred = static_cast<std::uint32_t>(std::min<std::size_t>(transferred, UINT32_MAX));
    result.complete = readSucceeded && transferred == rangeSize &&
        expected.size() == rangeSize && observed.size() >= rangeSize;
    if (!result.complete) return result;
    result.equal = std::equal(expected.begin(), expected.end(), observed.begin());
    if (!result.equal) {
        const auto it = std::mismatch(expected.begin(), expected.end(), observed.begin()).first;
        result.firstMismatch = static_cast<std::uint32_t>(it - expected.begin());
    }
    return result;
}

inline WorkingSetBits decodeWorkingSet(std::uint32_t attributes) noexcept {
    return WorkingSetBits{
        (attributes & 0x1u) != 0,
        (attributes & (1u << 15)) != 0,
        static_cast<std::uint8_t>((attributes >> 1) & 0x7u),
        static_cast<std::uint16_t>((attributes >> 4) & 0x7FFu)
    };
}

struct IdentityFacts {
    bool pid = false;
    bool creationTime = false;
    bool architecture = false;
    bool executablePath = false;
    bool executableHash = false;
    bool peIdentity = false;
    bool modulePath = false;
    bool imageBase = false;
};

inline bool authenticated(const IdentityFacts& facts) noexcept {
    return facts.pid && facts.creationTime && facts.architecture && facts.executablePath &&
        facts.executableHash && facts.peIdentity && facts.modulePath && facts.imageBase;
}

} // namespace grandmp::windows::alpha09_memory_reader
