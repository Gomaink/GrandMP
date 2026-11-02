#pragma once
#include "ped_smoke_addresses.hpp"
#include "ped_smoke_runtime_read.hpp"
#include <cstdio>
#include <cstring>
#include <array>

namespace grandmp::windows::ped_smoke {
// Failure evidence only: no entity pointers, memory contents, or execution lease.
struct CodeIdentityFailure {
    const char* function = "none";
    const char* category = "none";
    std::uint32_t address = 0, size = 0, mismatchOffset = 0;
    DWORD error = 0, expectedProtection = 0, alternateProtection = 0, observedProtection = 0;
    std::size_t transferred = 0;
};
inline bool identityRefuse(CodeIdentityFailure& f, const char* category,
                           std::uint32_t address, std::size_t size, DWORD error = 0) noexcept;

// The physical capture authenticated this exact Hoodlum code window and its
// two image pages. This is deliberately not a general PAGE_EXECUTE_WRITECOPY
// allowance. Runtime bytes still require a complete comparison with disk.
inline constexpr std::uint32_t hoodlumCowWindowAddress = 0x53BEE0;
inline constexpr std::size_t hoodlumCowWindowSize = 380;
inline constexpr std::array<std::uint32_t, 2> hoodlumCowPages{0x53B000, 0x53C000};
inline constexpr std::size_t hoodlumPageSize = 0x1000;
inline constexpr std::array<CodeRegion, 5> hoodlumCowCodeWindows{{
    {hoodlumCowWindowAddress, static_cast<std::uint32_t>(hoodlumCowWindowSize)},
    {0x53BC60, 0x1A}, // background focus-call bytes, fully disk-compared by its caller
    {0x53BEE6, 5}, // background input CALL, fully disk-compared by its caller
    {phaseCall + 5, 0x53C09A - phaseCall - 5},
    {phaseCall, 5}
}};
inline bool hoodlumCowCodeWindow(std::uint32_t at, std::size_t size) noexcept {
    for (const auto& region : hoodlumCowCodeWindows)
        if (region.address == at && region.size == size) return true;
    return false;
}

struct CodePageObservation {
    std::uint32_t queriedAddress = 0;
    std::uint64_t baseAddress = 0, regionSize = 0;
    std::uintptr_t allocationBase = 0;
    DWORD state = 0, type = 0, protection = 0, allocationProtection = 0;
    bool queryComplete = false;
    DWORD queryError = 0;
    bool workingSetComplete = false, workingSetValid = false, workingSetShared = false;
    DWORD workingSetError = 0;
};

inline bool identityHoodlumCowPages(CodeIdentityFailure& f, std::uint32_t at, std::size_t size,
                                     const std::array<CodePageObservation, 2>& pages,
                                     bool hoodlumIdentityAuthenticated,
                                     const std::array<bool, 2>& privatePageOwnedByExactPatch) noexcept {
    if (!hoodlumCowCodeWindow(at, size))
        return identityRefuse(f, "cow_window_not_allowed", at, size);
    if (!hoodlumIdentityAuthenticated)
        return identityRefuse(f, "hoodlum_identity_unavailable", at, size);
    for (std::size_t i = 0; i < pages.size(); ++i) {
        const auto& page = pages[i];
        const auto expectedAddress = hoodlumCowPages[i];
        if (!page.queryComplete)
            return identityRefuse(f, "page_query_failed", expectedAddress, hoodlumPageSize, page.queryError);
        if (page.queriedAddress != expectedAddress)
            return identityRefuse(f, "page_query_address", expectedAddress, hoodlumPageSize);
        if (page.state != MEM_COMMIT)
            return identityRefuse(f, "page_state", expectedAddress, hoodlumPageSize);
        if (page.type != MEM_IMAGE)
            return identityRefuse(f, "page_type", expectedAddress, hoodlumPageSize);
        if (page.allocationBase != imageBase)
            return identityRefuse(f, "image_allocation", expectedAddress, hoodlumPageSize);
        if (page.protection != PAGE_EXECUTE_WRITECOPY ||
            page.allocationProtection != PAGE_EXECUTE_WRITECOPY) {
            if (!std::strcmp(f.category, "none")) {
                f.expectedProtection = PAGE_EXECUTE_WRITECOPY;
                f.alternateProtection = 0;
                f.observedProtection = page.protection;
            }
            return identityRefuse(f, "page_protection", expectedAddress, hoodlumPageSize);
        }
        const auto regionEnd = page.baseAddress + page.regionSize;
        if (page.baseAddress > expectedAddress || regionEnd < std::uint64_t{expectedAddress} + hoodlumPageSize)
            return identityRefuse(f, "page_span", expectedAddress, hoodlumPageSize);
        if (!page.workingSetComplete)
            return identityRefuse(f, "working_set_query_failed", expectedAddress, hoodlumPageSize, page.workingSetError);
        if (!page.workingSetValid)
            return identityRefuse(f, "working_set_not_resident", expectedAddress, hoodlumPageSize);
        if (!page.workingSetShared && !privatePageOwnedByExactPatch[i])
            return identityRefuse(f, "working_set_private_unowned", expectedAddress, hoodlumPageSize);
    }
    return true;
}
inline bool identityRefuse(CodeIdentityFailure& f, const char* category,
                           std::uint32_t address, std::size_t size, DWORD error) noexcept {
    if (!std::strcmp(f.category, "none")) {
        f.category = category; f.address = address; f.size = static_cast<std::uint32_t>(size); f.error = error;
    }
    return false;
}
inline bool identityReadComplete(CodeIdentityFailure& f, std::uint32_t at,
                                  std::size_t size, MemoryRead result) noexcept {
    if (result.complete(size)) return true;
    if (!std::strcmp(f.category, "none")) f.transferred = result.transferred;
    return identityRefuse(f, result.success ? "read_incomplete" : "read_failed", at, size, result.error);
}
inline bool identityRead(CodeIdentityFailure& f, std::uint32_t at, void* output, std::size_t size) noexcept {
    return identityReadComplete(f, at, size, readMemory(at, output, size));
}
inline bool identityBytes(CodeIdentityFailure& f, std::uint32_t at, const void* actual,
                           const void* expected, std::size_t size, const char* category = "bytes_mismatch") noexcept {
    if (!expected) return identityRefuse(f, "disk_mapping_failed", at, size);
    const auto* a = static_cast<const unsigned char*>(actual);
    const auto* e = static_cast<const unsigned char*>(expected);
    for (std::size_t n = 0; n < size; ++n) if (a[n] != e[n]) {
        if (!std::strcmp(f.category, "none")) f.mismatchOffset = static_cast<std::uint32_t>(n);
        return identityRefuse(f, category, at, size);
    }
    return true;
}
inline bool identityPublishedBytes(CodeIdentityFailure& f, std::uint32_t at, const void* actual,
                                    const void* original, const void* installed, std::size_t size, bool published) noexcept {
    return identityBytes(f, at, actual, published ? installed : original, size);
}
inline bool identityPage(CodeIdentityFailure& f, std::uint32_t at, std::size_t size,
                          DWORD expected, DWORD alternate = 0, std::uintptr_t allocation = imageBase) noexcept {
    MEMORY_BASIC_INFORMATION page{};
    const auto count = VirtualQuery(reinterpret_cast<void*>(at), &page, sizeof(page));
    const DWORD error = count == sizeof(page) ? ERROR_SUCCESS : GetLastError();
    if (count != sizeof(page)) return identityRefuse(f, "page_query_failed", at, size, error);
    const auto refuse = [&](const char* category) {
        // Successful earlier windows must not leak metadata into a later failure.
        if (!std::strcmp(f.category, "none")) {
            f.expectedProtection = expected; f.alternateProtection = alternate; f.observedProtection = page.Protect;
        }
        return identityRefuse(f, category, at, size);
    };
    if (page.State != MEM_COMMIT) return refuse("page_state");
    if (page.Type != MEM_IMAGE) return refuse("page_type");
    if (reinterpret_cast<std::uintptr_t>(page.AllocationBase) != allocation)
        return refuse("image_allocation");
    if (page.Protect != expected && (!alternate || page.Protect != alternate))
        return refuse("page_protection");
    if (std::uint64_t{at} + size > reinterpret_cast<std::uintptr_t>(page.BaseAddress) + page.RegionSize)
        return refuse("page_span");
    return true;
}
inline bool formatCodeIdentityFailure(char* output, std::size_t capacity, const CodeIdentityFailure& f) noexcept {
    if (!output || !capacity) return false;
    const int n = std::snprintf(output, capacity,
        "identity_check function=%s address=%08lX size=%lu category=%s win32=%lu transferred=%zu expected_protect=%02lX alternate_protect=%02lX observed_protect=%02lX mismatch_offset=%lu",
        f.function, static_cast<unsigned long>(f.address), static_cast<unsigned long>(f.size), f.category,
        f.error, f.transferred, f.expectedProtection, f.alternateProtection, f.observedProtection,
        static_cast<unsigned long>(f.mismatchOffset));
    if (n < 0 || static_cast<std::size_t>(n) >= capacity) { output[0] = '\0'; return false; }
    return true;
}

// Same 20 factory windows; no address/extent changes. Separate from the two rdata checks.
inline constexpr std::array factoryCodeWindows{
    CodeRegion{0x60D5B0,0x2A0},CodeRegion{0x60CD20,0x2D5},
    CodeRegion{0x6093B0,0x1B0},CodeRegion{0x571880,0x100},
    CodeRegion{0x56F210,0x110},CodeRegion{0x5FB790,0x110},
    CodeRegion{0x5F7250,0x1C0},CodeRegion{0x5FBB70,0x20},
    CodeRegion{0x5FB280,0x90},CodeRegion{0x4018FD,0x20},
    CodeRegion{0x685750,0x70},CodeRegion{0x420B80,0x20},
    CodeRegion{0x1566E30,0x60},CodeRegion{0x536BE0,0x70},
    CodeRegion{0x563220,0x90},CodeRegion{0x542860,0x60},CodeRegion{0x5E47E0,0x98},
    CodeRegion{0x600B50,0xB0},CodeRegion{0x41863E,0x160},CodeRegion{0x411E70,0x4C}
};
static_assert(factoryCodeWindows.size() == 20);
inline bool factoryHoodlumWindow(std::uint32_t at, std::size_t size) noexcept {
    return at >= 0x1556000 && std::uint64_t{at} + size <= 0x1577000;
}
} // namespace grandmp::windows::ped_smoke
