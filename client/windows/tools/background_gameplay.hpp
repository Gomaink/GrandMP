#pragma once
#include "ped_smoke_code_identity.hpp"
#include "ped_smoke_patch_transaction.hpp"
#include <algorithm>
#include <initializer_list>

namespace grandmp::windows::background_gameplay {
namespace ps = ped_smoke;
namespace patch_transaction = ps::patch_transaction;
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1 && \
    defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1 && \
    defined(GRANDMP_CPLAYER_VARIANT) && GRANDMP_CPLAYER_VARIANT == 1 && \
    defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1 && \
    defined(GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD) && GRANDMP_NATIVE_SECOND_PLAYER_EXECUTION_BUILD == 1 && \
    defined(GRANDMP_PED_SMOKE_PHYSICAL_BUILD) && GRANDMP_PED_SMOKE_PHYSICAL_BUILD == 1
inline constexpr bool executionApproved = true;
#else
inline constexpr bool executionApproved = false;
#endif

// Exact admitted Hoodlum instructions, not offsets borrowed from a mod.
// docs/background-gameplay.md records the callers, flags and ABI evidence.
inline constexpr std::uint32_t loopCompare = 0x748A87, focusCall = 0x748063;
inline constexpr std::uint32_t padsCall = 0x53BEE6, updatePads = 0x541DD0;
inline constexpr std::array<ps::CodeRegion, 8> windows{{
    {0x747EB0, 0x5E0}, {0x53BC60, 0x1A}, {updatePads, 0x71},
    {0x40380D, 0xA}, {0x53F3C0, 0x16A}, {0x746060, 0x6}, {0x576B70, 0x381},
    {0x541A70, 0x160} // Original focus-loss Clear: pad states + keyboard/mouse history.
}};

// Resident and only changed on the admitted engine thread. No GTA global is
// spoofed. A partial installation keeps both thunks in their vanilla paths.
inline volatile LONG permit = 0;
inline void close() noexcept { InterlockedExchange(&permit, 0); }
inline void allow() noexcept { InterlockedExchange(&permit, 1); }

// Replaces one six-byte CMP with CALL rel32; NOP. Only the subsequent JE uses
// these flags. No registers/FPU/SIMD are modified; RET balances this new CALL.
__declspec(naked) inline void loopForeground() {
    __asm {
        cmp dword ptr [permit], 0
        jne permitted
        cmp dword ptr ds:[008D621Ch], ebx
    permitted:
        ret
    }
}
inline const std::uint32_t focusOriginal = 0x53BC60;
// This callsite alone requests the automatic menu. Manual ESC/menu paths are
// untouched. Tail dispatch preserves the original cdecl/no-arguments contract.
__declspec(naked) inline void onSetFocus() {
    __asm {
        cmp dword ptr [permit], 0
        jne suppressed
        jmp dword ptr [focusOriginal]
    suppressed:
        ret
    }
}

struct Failure {
    const char* category = "none";
    unsigned site = 0;
    DWORD error = 0, cleanupError = 0;
    patch_transaction::Failure transactionFailure = patch_transaction::Failure::none;
    bool rollbackComplete = false;
};
inline const patch_transaction::Backend& systemPatchBackend() noexcept {
    static const auto backend = patch_transaction::win32Backend();
    return backend;
}
class Patches final {
public:
    struct Site {
        std::uint32_t at = 0;
        std::size_t size = 0;
        std::array<std::byte, 6> original{}, replacement{};
        bool published = false;
    };
    bool installed() const noexcept { return installed_; }
    Failure failure() const noexcept { return failure_; }
    bool hasPublishedPatchInRange(std::uint32_t at, std::size_t size) const noexcept {
        const auto end = std::uint64_t{at} + size;
        for (const auto& site : sites_) if (site.published) {
            const auto siteEnd = std::uint64_t{site.at} + site.size;
            if (site.at < end && at < siteEnd) return true;
        }
        return false;
    }
    bool overlay(std::uint32_t at, std::byte* diskCopy, std::size_t size) const noexcept {
        for (const auto& site : sites_) if (site.published) {
            for (std::size_t n = 0; n < site.size; ++n) {
                const auto address = std::uint64_t{site.at} + n;
                if (address < at || address >= std::uint64_t{at} + size) continue;
                const auto offset = static_cast<std::size_t>(address - at);
                if (diskCopy[offset] != site.original[n]) return false;
                diskCopy[offset] = site.replacement[n];
            }
        }
        return true;
    }
    // Also exercised on a disposable host's own IMAGE instructions. The runtime
    // supplies only the three fixed admitted sites after its physical gate.
    bool publish(std::array<Site, 3> sites, void* image, bool context,
                 bool hoodlumIdentityAuthenticated = false,
                 const patch_transaction::Backend& backend = systemPatchBackend()) noexcept {
        if (attempted_) return false;
        attempted_ = true;
        if (!context) return refuse("context", 0);
        sites_ = sites;
        for (unsigned n = 0; n < sites_.size(); ++n)
            if (!matches(n, image, false, hoodlumIdentityAuthenticated, backend)) return false;
        for (unsigned n = 0; n < sites_.size(); ++n) {
            auto& site = sites_[n];
            // Do not publish a later site following an unknown modification.
            if (!matches(n, image, false, hoodlumIdentityAuthenticated, backend)) return false;
            const auto original = std::span<const std::byte>(site.original).first(site.size);
            const auto replacement = std::span<const std::byte>(site.replacement).first(site.size);
            const auto result = patch_transaction::apply(backend, site.at, original, replacement,
                hoodlumIdentityAuthenticated, reinterpret_cast<std::uintptr_t>(image));
            if (!result) {
                failure_.transactionFailure = result.failure;
                failure_.rollbackComplete = result.rollbackComplete;
                failure_.cleanupError = result.cleanupError;
                if (!result.rollbackComplete && !failure_.cleanupError &&
                    (result.bytesChanged || result.rollbackAttempted))
                    failure_.cleanupError = ERROR_INVALID_DATA;
                return refuse(patch_transaction::failureName(result.failure), n, result.error);
            }
            site.published = true; // Published only after exact readback and verified protection restore.
            if (!matches(n, image, true, hoodlumIdentityAuthenticated, backend)) return false;
        }
        installed_ = true;
        return true;
    }
    static Site call(std::uint32_t at, std::initializer_list<unsigned char> original,
                     std::uint32_t target) noexcept {
        Site site{}; site.at = at; site.size = original.size();
        if (site.size < 5 || site.size > site.original.size()) { site.size = 0; return site; }
        std::transform(original.begin(), original.end(), site.original.begin(),
            [](auto b) { return std::byte{b}; });
        site.replacement.fill(std::byte{0x90}); site.replacement[0] = std::byte{0xE8};
        const std::uint32_t relative = target - (at + 5); // x86 rel32 wraps in the 32-bit address space.
        std::memcpy(site.replacement.data() + 1, &relative, sizeof(relative));
        return site;
    }
private:
    bool refuse(const char* category, unsigned site, DWORD error = 0) noexcept {
        if (!std::strcmp(failure_.category, "none")) {
            failure_.category = category;
            failure_.site = site;
            failure_.error = error;
        }
        return false;
    }
    bool matches(unsigned n, void* image, bool published, bool hoodlumIdentityAuthenticated,
                 const patch_transaction::Backend& backend) noexcept {
        const auto& site = sites_[n]; ps::CodeIdentityFailure f{};
        std::array<std::byte, 6> bytes{};
        if (site.size < 5 || site.size > bytes.size()) return refuse("size", n);
        if (!ps::identityPage(f, site.at, site.size, PAGE_EXECUTE_READ, 0,
                reinterpret_cast<std::uintptr_t>(image))) {
            const bool cowFailure = !std::strcmp(f.category, "page_protection") ||
                !std::strcmp(f.category, "page_span");
            DWORD error = f.error;
            if (!cowFailure || f.observedProtection != PAGE_EXECUTE_WRITECOPY ||
                !hoodlumIdentityAuthenticated || !patch_transaction::exactCowSite(site.at, site.size) ||
                !backend.authorizeCow || !backend.authorizeCow(backend.context, site.at, site.size, error))
                return refuse(f.category, n, error);
        }
        if (!ps::identityRead(f, site.at, bytes.data(), site.size)) return refuse(f.category, n, f.error);
        if (std::memcmp(bytes.data(), (published ? site.replacement : site.original).data(), site.size))
            return refuse("bytes_mismatch", n);
        return true;
    }
    std::array<Site, 3> sites_{};
    bool attempted_ = false, installed_ = false;
    Failure failure_{};
};
} // namespace grandmp::windows::background_gameplay
