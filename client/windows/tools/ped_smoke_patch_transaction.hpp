#pragma once

#include "ped_smoke_addresses.hpp"

#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace grandmp::windows::ped_smoke::patch_transaction {

inline constexpr std::uint32_t backgroundPadsCall = 0x53BEE6;
inline constexpr DWORD writableProtection = PAGE_EXECUTE_READWRITE;
inline constexpr DWORD cowProtection = PAGE_EXECUTE_WRITECOPY;
inline constexpr std::size_t maxPatchBytes = 8;

// COW is confined to fixed Hook sites whose exact preimages are checked against
// the authenticated Hoodlum disk image before any write.
inline constexpr bool exactCowSite(std::uint32_t at, std::size_t size) noexcept {
    return (at == phaseCall && size == 5) || (at == backgroundPadsCall && size == 5) ||
        (at == 0x748063 && size == 5) || (at == 0x748A87 && size == 6);
}

struct Page {
    std::uintptr_t base = 0;
    std::size_t size = 0;
    std::uintptr_t allocationBase = 0;
    DWORD state = 0, type = 0, protection = 0, allocationProtection = 0;
};

struct Io {
    bool okay = false;
    std::size_t transferred = 0;
    DWORD error = ERROR_SUCCESS;
};

struct Backend {
    void* context = nullptr;
    bool (*query)(void*, std::uint32_t, Page&, DWORD&) noexcept = nullptr;
    bool (*protect)(void*, std::uint32_t, std::size_t, DWORD, DWORD&, DWORD&) noexcept = nullptr;
    Io (*read)(void*, std::uint32_t, std::span<std::byte>) noexcept = nullptr;
    Io (*write)(void*, std::uint32_t, std::span<const std::byte>) noexcept = nullptr;
    bool (*flush)(void*, std::uint32_t, std::size_t, DWORD&) noexcept = nullptr;
    bool (*authorizeCow)(void*, std::uint32_t, std::size_t, DWORD&) noexcept = nullptr;
    bool (*workingSet)(void*, std::uint32_t, bool&, bool&, DWORD&) noexcept = nullptr;
};

inline bool win32Query(void*, std::uint32_t address, Page& out, DWORD& error) noexcept {
    MEMORY_BASIC_INFORMATION value{};
    const SIZE_T count = VirtualQuery(reinterpret_cast<LPCVOID>(address), &value, sizeof(value));
    error = count == sizeof(value) ? ERROR_SUCCESS : GetLastError();
    if (count != sizeof(value)) return false;
    out = {reinterpret_cast<std::uintptr_t>(value.BaseAddress), value.RegionSize,
        reinterpret_cast<std::uintptr_t>(value.AllocationBase), value.State, value.Type,
        value.Protect, value.AllocationProtect};
    return true;
}

inline bool win32Protect(void*, std::uint32_t address, std::size_t size, DWORD requested,
                         DWORD& previous, DWORD& error) noexcept {
    const BOOL okay = VirtualProtect(reinterpret_cast<void*>(address), size, requested, &previous);
    error = okay ? ERROR_SUCCESS : GetLastError();
    return okay != FALSE;
}

inline Io win32Read(void*, std::uint32_t address, std::span<std::byte> output) noexcept {
    SIZE_T count = 0;
    const BOOL okay = ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address),
        output.data(), output.size(), &count);
    const DWORD error = okay ? ERROR_SUCCESS : GetLastError();
    return {okay != FALSE, count, error};
}

inline Io win32Write(void*, std::uint32_t address, std::span<const std::byte> input) noexcept {
    SIZE_T count = 0;
    const BOOL okay = WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address),
        input.data(), input.size(), &count);
    const DWORD error = okay ? ERROR_SUCCESS : GetLastError();
    return {okay != FALSE, count, error};
}

inline bool win32Flush(void*, std::uint32_t address, std::size_t size, DWORD& error) noexcept {
    const BOOL okay = FlushInstructionCache(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), size);
    error = okay ? ERROR_SUCCESS : GetLastError();
    return okay != FALSE;
}

inline bool win32WorkingSet(void*, std::uint32_t address, bool& valid, bool& shared, DWORD& error) noexcept {
    PSAPI_WORKING_SET_EX_INFORMATION value{};
    value.VirtualAddress = reinterpret_cast<void*>(address);
    const BOOL okay = K32QueryWorkingSetEx(GetCurrentProcess(), &value, sizeof(value));
    error = okay ? ERROR_SUCCESS : GetLastError();
    if (!okay) return false;
    valid = value.VirtualAttributes.Valid != 0;
    shared = value.VirtualAttributes.Shared != 0;
    return true;
}

inline Backend win32Backend(void* context = nullptr,
    bool (*authorizeCow)(void*, std::uint32_t, std::size_t, DWORD&) noexcept = nullptr) noexcept {
    return {context, win32Query, win32Protect, win32Read, win32Write, win32Flush,
        authorizeCow, win32WorkingSet};
}

enum class Failure : std::uint8_t {
    none, invalid_request, query_failed, page_identity, unexpected_protection,
    read_failed, preimage_mismatch, protect_failed, transition_mismatch,
    write_failed, write_partial, readback_failed, bytes_mismatch,
    cow_copy_not_private, flush_failed, protection_restore_failed,
    restore_transition_mismatch, post_restore_page, post_restore_readback,
    rollback_unavailable, rollback_write_failed, rollback_readback_failed,
    rollback_flush_failed, rollback_protection_failed, rollback_verification_failed
};

struct Result {
    Failure failure = Failure::none;
    DWORD error = ERROR_SUCCESS;
    DWORD cleanupError = ERROR_SUCCESS;
    DWORD originalProtection = 0;
    bool cow = false;
    bool bytesChanged = false;
    bool rollbackAttempted = false;
    bool rollbackComplete = false;
    constexpr explicit operator bool() const noexcept { return failure == Failure::none; }
};

inline const char* failureName(Failure failure) noexcept {
    switch (failure) {
    case Failure::none: return "none";
    case Failure::invalid_request: return "invalid_request";
    case Failure::query_failed: return "query_failed";
    case Failure::page_identity: return "page_identity";
    case Failure::unexpected_protection: return "unexpected_protection";
    case Failure::read_failed: return "read_failed";
    case Failure::preimage_mismatch: return "preimage_mismatch";
    case Failure::protect_failed: return "protect_failed";
    case Failure::transition_mismatch: return "transition_mismatch";
    case Failure::write_failed: return "write_failed";
    case Failure::write_partial: return "write_partial";
    case Failure::readback_failed: return "readback_failed";
    case Failure::bytes_mismatch: return "bytes_mismatch";
    case Failure::cow_copy_not_private: return "cow_copy_not_private";
    case Failure::flush_failed: return "flush_failed";
    case Failure::protection_restore_failed: return "protection_restore_failed";
    case Failure::restore_transition_mismatch: return "restore_transition_mismatch";
    case Failure::post_restore_page: return "post_restore_page";
    case Failure::post_restore_readback: return "post_restore_readback";
    case Failure::rollback_unavailable: return "rollback_unavailable";
    case Failure::rollback_write_failed: return "rollback_write_failed";
    case Failure::rollback_readback_failed: return "rollback_readback_failed";
    case Failure::rollback_flush_failed: return "rollback_flush_failed";
    case Failure::rollback_protection_failed: return "rollback_protection_failed";
    case Failure::rollback_verification_failed: return "rollback_verification_failed";
    }
    return "unknown";
}

inline bool rangeCovered(const Page& page, std::uint32_t address, std::size_t size,
                         std::uintptr_t expectedImageBase) noexcept {
    const auto end = std::uint64_t{address} + size;
    const auto regionEnd = std::uint64_t{page.base} + page.size;
    return page.state == MEM_COMMIT && page.type == MEM_IMAGE &&
        page.allocationBase == expectedImageBase && page.base <= address && regionEnd >= end;
}

inline bool queryPage(const Backend& backend, std::uint32_t address, std::size_t size,
                      Page& page, DWORD& error, std::uintptr_t expectedImageBase) noexcept {
    if (!backend.query || !backend.query(backend.context, address, page, error)) return false;
    return rangeCovered(page, address, size, expectedImageBase);
}

inline bool readExact(const Backend& backend, std::uint32_t address,
                      std::span<std::byte> output, DWORD& error) noexcept {
    if (!backend.read) return false;
    const auto result = backend.read(backend.context, address, output);
    error = result.error;
    return result.okay && result.transferred == output.size();
}

inline bool bytesEqual(const Backend& backend, std::uint32_t address,
                       std::span<const std::byte> expected, DWORD& error) noexcept {
    std::array<std::byte, maxPatchBytes> observed{};
    if (expected.empty() || expected.size() > observed.size() ||
        !readExact(backend, address, std::span<std::byte>(observed).first(expected.size()), error)) return false;
    return std::equal(expected.begin(), expected.end(), observed.begin());
}

inline bool workingSetPrivate(const Backend& backend, std::uint32_t address, DWORD& error) noexcept {
    bool valid = false, shared = true;
    if (!backend.workingSet || !backend.workingSet(backend.context, address, valid, shared, error)) return false;
    return valid && !shared;
}
inline bool workingSetShared(const Backend& backend, std::uint32_t address, DWORD& error) noexcept {
    bool valid = false, shared = false;
    if (!backend.workingSet || !backend.workingSet(backend.context, address, valid, shared, error)) return false;
    return valid && shared;
}

inline bool restoreProtectionOnly(const Backend& backend, std::uint32_t address, std::size_t size,
                                  DWORD original, Result& result,
                                  std::uintptr_t expectedImageBase,
                                  DWORD expectedCurrent = writableProtection,
                                  bool cowBacked = false) noexcept {
    DWORD observed = 0, error = ERROR_SUCCESS;
    if (!backend.protect || !backend.protect(backend.context, address, size, original, observed, error)) {
        result.cleanupError = error;
        return false;
    }
    if (observed != expectedCurrent) {
        result.cleanupError = ERROR_INVALID_DATA;
        return false;
    }
    Page page{};
    if (!queryPage(backend, address, size, page, error, expectedImageBase) || page.protection != original ||
        (cowBacked && page.allocationProtection != cowProtection)) {
        result.cleanupError = error ? error : ERROR_INVALID_DATA;
        return false;
    }
    return true;
}

inline bool knownMutation(std::span<const std::byte> original,
                          std::span<const std::byte> replacement,
                          std::span<const std::byte> observed,
                          std::size_t written) noexcept {
    if (observed.size() != original.size() || original.size() != replacement.size()) return false;
    if (std::equal(observed.begin(), observed.end(), original.begin())) return true;
    // WriteProcessMemory reports a prefix count. Only that exact prefix/suffix
    // pattern is considered attributable to this transaction for rollback.
    const auto prefix = std::min(written, original.size());
    for (std::size_t i = 0; i < observed.size(); ++i) {
        const auto expected = i < prefix ? replacement[i] : original[i];
        if (observed[i] != expected) return false;
    }
    return prefix != 0;
}

inline bool readArray(const Backend& backend, std::uint32_t address, std::size_t size,
                      std::array<std::byte, maxPatchBytes>& bytes, DWORD& error) noexcept {
    return size <= bytes.size() && readExact(backend, address, std::span<std::byte>(bytes).first(size), error);
}

inline bool rollback(const Backend& backend, std::uint32_t address, std::size_t size,
                     DWORD originalProtection, bool cow, std::span<const std::byte> original,
                     std::span<const std::byte> replacement, std::size_t written,
                     Result& result, std::uintptr_t expectedImageBase) noexcept {
    result.rollbackAttempted = true;
    Page page{};
    DWORD error = ERROR_SUCCESS;
    if (!queryPage(backend, address, size, page, error, expectedImageBase)) {
        result.cleanupError = error ? error : ERROR_INVALID_ADDRESS;
        return false;
    }

    std::array<std::byte, maxPatchBytes> observed{};
    if (!readArray(backend, address, size, observed, error)) {
        result.cleanupError = error ? error : ERROR_PARTIAL_COPY;
        return false;
    }
    if (!knownMutation(original, replacement, std::span<const std::byte>(observed).first(size), written)) {
        result.cleanupError = ERROR_INVALID_DATA;
        return false;
    }
    if (std::equal(observed.begin(), observed.begin() + size, original.begin())) {
        if (page.protection == originalProtection) {
            result.rollbackComplete = true;
            return true;
        }
        if (cow && originalProtection == PAGE_EXECUTE_READ && page.protection == cowProtection &&
            page.allocationProtection == cowProtection) {
            if (!restoreProtectionOnly(backend, address, size, originalProtection, result,
                    expectedImageBase, cowProtection, true)) return false;
            result.rollbackComplete = true;
            return true;
        }
        if (page.protection != writableProtection) {
            result.cleanupError = ERROR_INVALID_DATA;
            return false;
        }
    }

    if (cow) {
        if (page.protection == cowProtection) {
            DWORD old = 0;
            if (!backend.protect || !backend.protect(backend.context, address, size, writableProtection, old, error) ||
                old != cowProtection) {
                result.cleanupError = error ? error : ERROR_INVALID_DATA;
                return false;
            }
        } else if (page.protection != writableProtection) {
            result.cleanupError = ERROR_INVALID_DATA;
            return false;
        }
        if (!workingSetPrivate(backend, address, error)) {
            result.cleanupError = error ? error : ERROR_INVALID_DATA;
            return false;
        }
    } else if (page.protection == originalProtection) {
        DWORD old = 0;
        if (!backend.protect || !backend.protect(backend.context, address, size, writableProtection, old, error) ||
            old != originalProtection) {
            result.cleanupError = error ? error : ERROR_INVALID_DATA;
            return false;
        }
    } else if (page.protection != writableProtection) {
        result.cleanupError = ERROR_INVALID_DATA;
        return false;
    }

    const auto write = backend.write ? backend.write(backend.context, address, original) : Io{};
    if (!write.okay || write.transferred != original.size()) {
        result.cleanupError = write.error ? write.error : ERROR_WRITE_FAULT;
        return false;
    }
    if (!bytesEqual(backend, address, original, error)) {
        result.cleanupError = error ? error : ERROR_INVALID_DATA;
        return false;
    }
    if (!backend.flush || !backend.flush(backend.context, address, size, error)) {
        result.cleanupError = error ? error : ERROR_INVALID_DATA;
        return false;
    }
    if (!restoreProtectionOnly(backend, address, size, originalProtection, result,
            expectedImageBase, writableProtection, cow)) return false;

    Page finalPage{};
    if (!queryPage(backend, address, size, finalPage, error, expectedImageBase) || finalPage.protection != originalProtection ||
        !bytesEqual(backend, address, original, error) ||
        (cow && (finalPage.allocationProtection != cowProtection || !workingSetPrivate(backend, address, error)))) {
        result.cleanupError = error ? error : ERROR_INVALID_DATA;
        return false;
    }
    result.rollbackComplete = true;
    result.bytesChanged = false;
    return true;
}

inline Result apply(const Backend& backend, std::uint32_t address,
                    std::span<const std::byte> original,
                    std::span<const std::byte> replacement,
                    bool hoodlumIdentityAuthenticated,
                    std::uintptr_t expectedImageBase = imageBase) noexcept {
    Result result{};
    if (!backend.query || !backend.protect || !backend.read || !backend.write || !backend.flush ||
        !address || original.empty() || original.size() != replacement.size() ||
        original.size() > maxPatchBytes || (original.size() != 5 && original.size() != 6)) {
        result.failure = Failure::invalid_request;
        return result;
    }

    Page initial{};
    DWORD error = ERROR_SUCCESS;
    if (!queryPage(backend, address, original.size(), initial, error, expectedImageBase)) {
        result.failure = error ? Failure::query_failed : Failure::page_identity;
        result.error = error;
        return result;
    }
    result.originalProtection = initial.protection;
    result.cow = initial.protection == cowProtection || initial.allocationProtection == cowProtection;
    const bool cowAuthorized = result.cow && hoodlumIdentityAuthenticated &&
        initial.allocationProtection == cowProtection &&
        backend.authorizeCow && backend.authorizeCow(backend.context, address, original.size(), error);
    if (initial.protection != PAGE_EXECUTE_READ && !(initial.protection == cowProtection && cowAuthorized)) {
        result.failure = Failure::unexpected_protection;
        result.error = error;
        return result;
    }
    if (initial.protection == PAGE_EXECUTE_READ && initial.allocationProtection == cowProtection && !cowAuthorized) {
        result.failure = Failure::unexpected_protection;
        result.error = error;
        return result;
    }
    if (!bytesEqual(backend, address, original, error)) {
        result.failure = error ? Failure::read_failed : Failure::preimage_mismatch;
        result.error = error;
        return result;
    }

    if (result.cow) {
        // Certain executable MEM_IMAGE pages report RX at rest but transition
        // to EXECUTE_WRITECOPY when RWX is requested. Only accept this for an
        // exact admitted site and a verified preimage.
        if (initial.protection == PAGE_EXECUTE_READ) {
            DWORD old = 0;
            if (!backend.protect(backend.context, address, original.size(), writableProtection, old, error)) {
                result.failure = Failure::protect_failed; result.error = error; return result;
            }
            if (old != PAGE_EXECUTE_READ) {
                result.failure = Failure::unexpected_protection; result.error = ERROR_INVALID_DATA;
                Page changed{};
                DWORD queryError = ERROR_SUCCESS;
                const DWORD current = queryPage(backend, address, original.size(), changed, queryError, expectedImageBase)
                    ? changed.protection : writableProtection;
                restoreProtectionOnly(backend, address, original.size(), old, result,
                    expectedImageBase, current, true);
                return result;
            }
            Page transitioned{};
            const bool pageQueried = queryPage(backend, address, original.size(), transitioned, error, expectedImageBase);
            const bool cowTransition = pageQueried && transitioned.protection == cowProtection &&
                transitioned.allocationProtection == cowProtection;
            const bool ownedPrivateTransition = pageQueried && transitioned.protection == writableProtection &&
                transitioned.allocationProtection == cowProtection && workingSetPrivate(backend, address, error) &&
                backend.authorizeCow && backend.authorizeCow(backend.context, address, original.size(), error);
            const bool sharedOrOwnedCow = cowTransition && (workingSetShared(backend, address, error) ||
                (backend.authorizeCow && backend.authorizeCow(backend.context, address, original.size(), error)));
            if ((!cowTransition && !ownedPrivateTransition) || (cowTransition && !sharedOrOwnedCow)) {
                result.failure = Failure::cow_copy_not_private; result.error = error;
                Page currentPage{};
                DWORD queryError = ERROR_SUCCESS;
                const DWORD current = queryPage(backend, address, original.size(), currentPage,
                    queryError, expectedImageBase) ? currentPage.protection : cowProtection;
                if (!restoreProtectionOnly(backend, address, original.size(), PAGE_EXECUTE_READ, result,
                        expectedImageBase, current, true) && !result.cleanupError)
                    result.cleanupError = queryError ? queryError : ERROR_INVALID_DATA;
                return result;
            }
            if (!bytesEqual(backend, address, original, error)) {
                result.failure = error ? Failure::read_failed : Failure::preimage_mismatch;
                result.error = error;
                if (!restoreProtectionOnly(backend, address, original.size(), PAGE_EXECUTE_READ, result,
                        expectedImageBase, cowProtection, true) && !result.cleanupError)
                    result.cleanupError = ERROR_INVALID_DATA;
                return result;
            }
        }
        const auto write = backend.write(backend.context, address, replacement);
        result.bytesChanged = write.transferred != 0;
        std::array<std::byte, maxPatchBytes> observed{};
        const bool readOkay = readArray(backend, address, original.size(), observed, error);
        const auto observedSpan = std::span<const std::byte>(observed).first(original.size());
        const bool replacementObserved = readOkay && std::equal(replacement.begin(), replacement.end(), observed.begin());
        const bool mutationKnown = readOkay && knownMutation(original, replacement, observedSpan,
                                                              write.transferred);
        if (!write.okay || write.transferred != replacement.size() || !readOkay || !replacementObserved) {
            result.failure = write.transferred != replacement.size() ? Failure::write_partial :
                !write.okay ? Failure::write_failed :
                !readOkay ? Failure::readback_failed : Failure::bytes_mismatch;
            result.error = write.error ? write.error : error;
            if (mutationKnown)
                rollback(backend, address, original.size(), result.originalProtection, true, original, replacement,
                    write.transferred, result, expectedImageBase);
            else result.rollbackComplete = readOkay && std::equal(observed.begin(), observed.begin() + original.size(), original.begin());
            return result;
        }

        Page privatePage{};
        if (!queryPage(backend, address, original.size(), privatePage, error, expectedImageBase) ||
            privatePage.protection != writableProtection || privatePage.allocationProtection != cowProtection ||
            !workingSetPrivate(backend, address, error)) {
            result.failure = Failure::cow_copy_not_private;
            result.error = error;
            rollback(backend, address, original.size(), result.originalProtection, true, original, replacement,
                write.transferred, result, expectedImageBase);
            return result;
        }
        if (!backend.flush(backend.context, address, original.size(), error)) {
            result.failure = Failure::flush_failed; result.error = error;
            rollback(backend, address, original.size(), result.originalProtection, true, original, replacement,
                write.transferred, result, expectedImageBase);
            return result;
        }
        DWORD old = 0;
        if (!backend.protect(backend.context, address, original.size(), result.originalProtection, old, error) ||
            old != writableProtection) {
            result.failure = Failure::protection_restore_failed; result.error = error;
            rollback(backend, address, original.size(), result.originalProtection, true, original, replacement,
                write.transferred, result, expectedImageBase);
            return result;
        }
    } else {
        DWORD old = 0;
        if (!backend.protect(backend.context, address, original.size(), writableProtection, old, error)) {
            result.failure = Failure::protect_failed; result.error = error; return result;
        }
        if (old != PAGE_EXECUTE_READ) {
            result.failure = Failure::unexpected_protection;
            result.error = ERROR_INVALID_DATA;
            restoreProtectionOnly(backend, address, original.size(), old, result, expectedImageBase);
            return result;
        }
        Page writablePage{};
        if (!queryPage(backend, address, original.size(), writablePage, error, expectedImageBase) ||
            writablePage.protection != writableProtection || !bytesEqual(backend, address, original, error)) {
            result.failure = error ? Failure::query_failed : Failure::preimage_mismatch;
            result.error = error;
            if (!restoreProtectionOnly(backend, address, original.size(), PAGE_EXECUTE_READ, result, expectedImageBase))
                result.cleanupError = result.cleanupError ? result.cleanupError : ERROR_INVALID_DATA;
            return result;
        }
        const auto write = backend.write(backend.context, address, replacement);
        result.bytesChanged = write.transferred != 0;
        if (!write.okay || write.transferred != replacement.size()) {
            result.failure = write.transferred != replacement.size() ? Failure::write_partial : Failure::write_failed;
            result.error = write.error;
            rollback(backend, address, original.size(), PAGE_EXECUTE_READ, false, original, replacement,
                write.transferred, result, expectedImageBase);
            return result;
        }
        if (!bytesEqual(backend, address, replacement, error)) {
            result.failure = error ? Failure::readback_failed : Failure::bytes_mismatch;
            result.error = error;
            rollback(backend, address, original.size(), PAGE_EXECUTE_READ, false, original, replacement,
                write.transferred, result, expectedImageBase);
            return result;
        }
        if (!backend.flush(backend.context, address, original.size(), error)) {
            result.failure = Failure::flush_failed; result.error = error;
            rollback(backend, address, original.size(), PAGE_EXECUTE_READ, false, original, replacement,
                write.transferred, result, expectedImageBase);
            return result;
        }
        DWORD oldAfter = 0;
        if (!backend.protect(backend.context, address, original.size(), PAGE_EXECUTE_READ, oldAfter, error) ||
            oldAfter != writableProtection) {
            result.failure = Failure::protection_restore_failed; result.error = error;
            rollback(backend, address, original.size(), PAGE_EXECUTE_READ, false, original, replacement,
                write.transferred, result, expectedImageBase);
            return result;
        }
    }

    Page finalPage{};
    const auto expectedFinalProtection = result.originalProtection;
    if (!queryPage(backend, address, original.size(), finalPage, error, expectedImageBase) ||
        finalPage.protection != expectedFinalProtection ||
        (result.cow && (finalPage.allocationProtection != cowProtection ||
            !workingSetPrivate(backend, address, error)))) {
        result.failure = Failure::post_restore_page; result.error = error;
        rollback(backend, address, original.size(), result.originalProtection, result.cow,
            original, replacement, replacement.size(), result, expectedImageBase);
        return result;
    }
    if (!bytesEqual(backend, address, replacement, error)) {
        result.failure = error ? Failure::readback_failed : Failure::post_restore_readback;
        result.error = error;
        // Never overwrite bytes that no longer equal this transaction's replacement.
        return result;
    }
    return result;
}

} // namespace grandmp::windows::ped_smoke::patch_transaction
