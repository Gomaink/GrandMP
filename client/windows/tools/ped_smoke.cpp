#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#include <psapi.h>

#include "grandmp/windows/executable_detection.hpp"
#include "ped_smoke_addresses.hpp"
#include "ped_smoke_install_diagnostics.hpp"
#include "ped_smoke_phase.hpp"
#include "ped_smoke_state.hpp"
#include "ped_smoke_spawn.hpp"
#include "ped_smoke_move.hpp"
#include "ped_smoke_move_native.hpp"
#include "ped_smoke_runtime_read.hpp"
#include "ped_smoke_code_identity.hpp"
#include "ped_smoke_patch_transaction.hpp"
#include "ped_smoke_admission_diagnostics.hpp"
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1 && \
    (!defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) || GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD != 1 || \
     !defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) || GRANDMP_NATIVE_INPUT_REPLAY_BUILD != 1 || !defined(GRANDMP_CPLAYER_VARIANT))
#error Background gameplay requires the isolated Alpha 0.9 capture/replay variant
#endif
#if defined(GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD) && GRANDMP_PASSIVE_MOVEMENT_DIAGNOSTIC_BUILD == 1
#if !defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) || GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD != 1
#error Passive movement diagnostics require the exclusive task diagnostic variant
#endif
#endif
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
#if !defined(GRANDMP_REMOTE_PED_VARIANT) || !defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
#error Task diagnostics require the remote one-walk variant
#endif
#include "native_diagnostic_integration.hpp"
#include "native_diagnostic_windows.hpp"
#include "native_diagnostic_worker.hpp"
#endif
#if defined(GRANDMP_REMOTE_PED_VARIANT)
#include "remote_ped.hpp"
#include "remote_ped_network.hpp"
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
#include "remote_ped_move.hpp"
#endif
#include "grandmp/windows/observer_network.hpp"
#endif

#if defined(GRANDMP_CPLAYER_VARIANT)
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1 && \
    (!defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) || GRANDMP_NATIVE_INPUT_REPLAY_BUILD != 1)
#error Local player capture requires the isolated CPlayerPed input replay variant
#endif
#include "native_second_player.hpp"
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
#include "native_player_replay.hpp"
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
#include "local_player_capture.hpp"
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
#include "background_gameplay.hpp"
#endif
#endif
#endif
#include "remote_ped_network.hpp"
#include <optional>
#endif

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <filesystem>
#include <intrin.h>
#include <vector>

#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
#include "native_player_bootstrap_diagnostic.hpp"
namespace bd = grandmp::windows::bootstrap_diagnostic;
extern "C" {
constinit bd::Data GrandMPNativePlayerBootstrapDiagnostic{};
}
#define BOOT_NOTE(event, ...) do { using namespace bd; bd::publish(GrandMPNativePlayerBootstrapDiagnostic, bd::Event::event, __VA_ARGS__); } while(false)
#else
#define BOOT_NOTE(...) ((void)0)
#endif

static_assert(sizeof(void*) == 4, "Ped smoke requires Win32 x86");
namespace {
namespace ps = grandmp::windows::ped_smoke;
HMODULE module = nullptr;
std::atomic<unsigned> preparation{0}; // 0 unloaded/pre-init, 1 preparing, 2 blocked, 3 installed, 4 failed
std::atomic<unsigned> callbacks{0};
std::atomic<bool> removalRequested{false};
std::atomic<bool> hookPublished{false}, runtimeDisabled{false};
// Bootstrap handshake: 0 idle, 1 armed, 2 executing on window thread, 3 done, 4 cancelled.
std::atomic<unsigned> installation{0};
std::atomic<std::uint32_t> installCandidateFailure{static_cast<std::uint32_t>(ps::InstallFailure::none)};
std::atomic<std::uint32_t> installCandidateError{0};
std::atomic<bool> installCandidateHasError{false};
std::atomic<std::uint32_t> installCandidateCleanupFailure{static_cast<std::uint32_t>(ps::InstallFailure::none)};
std::atomic<std::uint32_t> installCandidateCleanupError{0};
std::atomic<bool> installCandidateCleanupHasError{false};
ps::CallBytes installedCall{};
#if !defined(GRANDMP_CPLAYER_VARIANT)
ps::Experiment experiment;
ps::Movement movement;
#endif
DWORD gameThread = 0;
bool phaseDispatchActive = false; // Accessed only after authenticating gameThread.
HANDLE executableFile = INVALID_HANDLE_VALUE; // Retained for installed profile lifetime.
std::vector<std::byte> executable;
std::atomic<bool> hoodlumIdentityAuthenticated{false};
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
namespace bg = grandmp::windows::background_gameplay;
bg::Patches backgroundPatches; // Resident with the pinned phase/input wrappers.
#endif

void writeLog(const char* state, const char* reason, std::uintptr_t ped = 0) noexcept {
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
    // Only the first log attempt is traced. Never add a fallback file or retry.
    const bool trace = InterlockedCompareExchange(&GrandMPNativePlayerBootstrapDiagnostic.logClaimed,1,0)==0;
    if(trace) BOOT_NOTE(module_path,entered);
#endif
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, 32768);
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
    const DWORD pathError=(!length || length>=32768) ? GetLastError() : ERROR_SUCCESS;
    if(trace) BOOT_NOTE(module_path, (!length || length>=32768) ? failed : succeeded, pathError, static_cast<LONG>(length));
#endif
    if (!length || length >= 32768) return;
    wchar_t* slash = std::wcsrchr(path, L'\\');
    if (!slash) {
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
        if(trace) BOOT_NOTE(log_path,failed,0,1); // Synthetic detail, no Win32 call failed.
#endif
        return;
    }
    constexpr wchar_t filename[] =
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
        L"GrandMPNativePlayerSyncExperimental.log";
#elif defined(GRANDMP_CPLAYER_VARIANT)
        L"GrandMPNativePlayerExperimental.log";
#elif defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        L"GrandMPRemotePedMoveExperimental.log";
#elif defined(GRANDMP_REMOTE_PED_VARIANT)
        L"GrandMPRemotePedExperimental.log";
#else
        L"GrandMPPedSmoke.log";
#endif
    if (slash - path + 1 + std::size(filename) > std::size(path)) {
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
        if(trace) BOOT_NOTE(log_path,failed,0,2);
#endif
        return;
    }
    wcscpy_s(slash + 1, std::size(path) - (slash - path + 1), filename);
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
    if(trace) { BOOT_NOTE(log_path,succeeded);BOOT_NOTE(create_file,entered); }
#endif
    const HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS, 0, nullptr);
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
    const DWORD fileError=file==INVALID_HANDLE_VALUE ? GetLastError() : ERROR_SUCCESS;
    if(trace) BOOT_NOTE(create_file, file==INVALID_HANDLE_VALUE ? failed : succeeded, fileError);
#endif
    if (file == INVALID_HANDLE_VALUE) return;
    char line[512]{};
    const int count = std::snprintf(line, sizeof(line),
        "[GrandMPPedSmoke] state=%s reason=%s pid=%lu tid=%lu ped=%08lX callback=%u\r\n",
        state, reason, GetCurrentProcessId(), GetCurrentThreadId(), ped, callbacks.load());
    DWORD written = 0;
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
    if (count > 0 && count < sizeof(line)) {
        if(trace) BOOT_NOTE(write_file,entered);
        const BOOL okay=WriteFile(file,line,static_cast<DWORD>(count),&written,nullptr);
        const DWORD error=okay ? ERROR_SUCCESS : GetLastError();
        if(trace) BOOT_NOTE(write_file, (!okay || written!=static_cast<DWORD>(count)) ? failed : succeeded,
                            error,static_cast<LONG>(written));
    } else if(trace) { BOOT_NOTE(write_file,failed,0,-1); }
#else
    if (count > 0 && count < sizeof(line)) WriteFile(file, line, static_cast<DWORD>(count), &written, nullptr);
#endif
    CloseHandle(file);
}

template<class... Args> void writeSpawnLog(const char* format, Args... args) noexcept {
    char detail[208]{};
    const int count = std::snprintf(detail, sizeof(detail), format, args...);
    if (count > 0 && count < sizeof(detail)) writeLog("spawn_probe", detail);
}

template<class T> bool read(std::uintptr_t address, T& value) noexcept {
    return ps::readMemory(address, &value, sizeof(value)).complete(sizeof(value));
}
bool imagePage(std::uint32_t address, std::size_t size, bool code) noexcept {
    MEMORY_BASIC_INFORMATION page{};
    return VirtualQuery(reinterpret_cast<LPCVOID>(address), &page, sizeof(page)) == sizeof(page) &&
        page.State == MEM_COMMIT && page.Type == MEM_IMAGE &&
        reinterpret_cast<std::uintptr_t>(page.AllocationBase) == ps::imageBase &&
        (code ? page.Protect == PAGE_EXECUTE_READ : page.Protect == PAGE_READONLY) &&
        std::uint64_t{address} + size <= reinterpret_cast<std::uintptr_t>(page.BaseAddress) + page.RegionSize;
}

bool observeHoodlumCowPages(ps::CodeIdentityFailure& failure, std::uint32_t address, std::size_t size) noexcept {
    std::array<ps::CodePageObservation, 2> observations{};
    for (std::size_t i = 0; i < observations.size(); ++i) {
        auto& out = observations[i];
        out.queriedAddress = ps::hoodlumCowPages[i];
        MEMORY_BASIC_INFORMATION page{};
        const SIZE_T queried = VirtualQuery(reinterpret_cast<LPCVOID>(out.queriedAddress), &page, sizeof(page));
        const DWORD queryError = queried == sizeof(page) ? ERROR_SUCCESS : GetLastError();
        out.queryComplete = queried == sizeof(page);
        out.queryError = queryError;
        if (out.queryComplete) {
            out.baseAddress = reinterpret_cast<std::uintptr_t>(page.BaseAddress);
            out.regionSize = page.RegionSize;
            out.allocationBase = reinterpret_cast<std::uintptr_t>(page.AllocationBase);
            out.state = page.State;
            out.type = page.Type;
            out.protection = page.Protect;
            out.allocationProtection = page.AllocationProtect;
        }

        PSAPI_WORKING_SET_EX_INFORMATION workingSet{};
        workingSet.VirtualAddress = reinterpret_cast<void*>(out.queriedAddress);
        const BOOL workingSetOkay = K32QueryWorkingSetEx(GetCurrentProcess(), &workingSet,
            static_cast<DWORD>(sizeof(workingSet)));
        const DWORD workingSetError = workingSetOkay ? ERROR_SUCCESS : GetLastError();
        out.workingSetComplete = workingSetOkay != FALSE;
        out.workingSetError = workingSetError;
        if (out.workingSetComplete) {
            out.workingSetValid = workingSet.VirtualAttributes.Valid != 0;
            out.workingSetShared = workingSet.VirtualAttributes.Shared != 0;
        }
    }

    std::array<bool, 2> privatePageOwnedByExactPatch{};
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
    privatePageOwnedByExactPatch[0] = backgroundPatches.hasPublishedPatchInRange(
        ps::hoodlumCowPages[0], ps::hoodlumPageSize);
    privatePageOwnedByExactPatch[1] = backgroundPatches.hasPublishedPatchInRange(
        ps::hoodlumCowPages[1], ps::hoodlumPageSize);
#endif
    privatePageOwnedByExactPatch[1] = privatePageOwnedByExactPatch[1] || hookPublished.load(std::memory_order_acquire);
    return ps::identityHoodlumCowPages(failure, address, size, observations,
        hoodlumIdentityAuthenticated.load(std::memory_order_acquire), privatePageOwnedByExactPatch);
}

bool identityCodePage(ps::CodeIdentityFailure& failure, std::uint32_t address, std::size_t size) noexcept {
    if (ps::identityPage(failure, address, size, PAGE_EXECUTE_READ)) return true;
    const bool expectedCowProtectionFailure = !std::strcmp(failure.category, "page_protection") ||
        !std::strcmp(failure.category, "page_span");
    if (!expectedCowProtectionFailure || failure.observedProtection != PAGE_EXECUTE_WRITECOPY ||
        !ps::hoodlumCowCodeWindow(address, size)) return false;
    ps::CodeIdentityFailure cowFailure{};
    cowFailure.function = failure.function;
    if (observeHoodlumCowPages(cowFailure, address, size)) {
        failure = {};
        failure.function = cowFailure.function;
        return true;
    }
    failure = cowFailure;
    return false;
}

bool authorizeCowPatch(void*, std::uint32_t address, std::size_t size, DWORD& error) noexcept {
    if (!hoodlumIdentityAuthenticated.load(std::memory_order_acquire) ||
        !ps::patch_transaction::exactCowSite(address, size)) {
        error = ERROR_ACCESS_DENIED;
        return false;
    }
    ps::CodeIdentityFailure failure{};
    failure.function = "authorizeCowPatch";
    if (identityCodePage(failure, address, size)) { error = ERROR_SUCCESS; return true; }

    // Fixed background callsites can be RX initially while their image section
    // is COW-backed. After one exact patch privatizes their shared page, permit
    // the next exact preimage on that same page only while this process owns a
    // previously published patch in the page.
    MEMORY_BASIC_INFORMATION page{};
    const auto count = VirtualQuery(reinterpret_cast<void*>(address), &page, sizeof(page));
    if (count != sizeof(page) || page.State != MEM_COMMIT || page.Type != MEM_IMAGE ||
        reinterpret_cast<std::uintptr_t>(page.AllocationBase) != ps::imageBase ||
        (page.Protect != PAGE_EXECUTE_WRITECOPY && page.Protect != PAGE_EXECUTE_READWRITE) ||
        page.AllocationProtect != PAGE_EXECUTE_WRITECOPY) {
        error = failure.error ? failure.error : ERROR_INVALID_DATA;
        return false;
    }
    const auto pageBase = reinterpret_cast<std::uintptr_t>(page.BaseAddress);
    if (pageBase > address || pageBase + page.RegionSize < std::uint64_t{address} + size) {
        error = ERROR_INVALID_ADDRESS;
        return false;
    }
    PSAPI_WORKING_SET_EX_INFORMATION workingSet{};
    workingSet.VirtualAddress = reinterpret_cast<void*>(address);
    const BOOL queried = K32QueryWorkingSetEx(GetCurrentProcess(), &workingSet, sizeof(workingSet));
    const DWORD queryError = queried ? ERROR_SUCCESS : GetLastError();
    if (!queried || !workingSet.VirtualAttributes.Valid) {
        error = queryError ? queryError : ERROR_INVALID_DATA;
        return false;
    }
    if (workingSet.VirtualAttributes.Shared && page.Protect == PAGE_EXECUTE_WRITECOPY) {
        error = ERROR_SUCCESS; return true;
    }
    bool owned = pageBase == ps::phaseCall - (ps::phaseCall % ps::hoodlumPageSize) &&
        hookPublished.load(std::memory_order_acquire);
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
    owned = owned || backgroundPatches.hasPublishedPatchInRange(
        static_cast<std::uint32_t>(pageBase), ps::hoodlumPageSize);
#endif
    error = owned ? ERROR_SUCCESS : ERROR_ACCESS_DENIED;
    return owned;
}

// Map an admitted PE32 file RVA to raw bytes, with bounds at every step.
const std::byte* diskBytes(std::uint32_t address, std::size_t size) noexcept {
    if (executable.size() < sizeof(IMAGE_DOS_HEADER) || address < ps::imageBase) return nullptr;
    IMAGE_DOS_HEADER dos{};
    std::memcpy(&dos, executable.data(), sizeof(dos));
    if (dos.e_lfanew < 0 || std::uint64_t(dos.e_lfanew) + sizeof(IMAGE_NT_HEADERS32) > executable.size()) return nullptr;
    IMAGE_NT_HEADERS32 nt{};
    std::memcpy(&nt, executable.data() + dos.e_lfanew, sizeof(nt));
    const auto table = std::uint64_t(dos.e_lfanew) + 24 + nt.FileHeader.SizeOfOptionalHeader;
    if (nt.FileHeader.NumberOfSections > 32 || table + nt.FileHeader.NumberOfSections * sizeof(IMAGE_SECTION_HEADER) > executable.size()) return nullptr;
    const auto rva = address - ps::imageBase;
    for (unsigned i = 0; i < nt.FileHeader.NumberOfSections; ++i) {
        IMAGE_SECTION_HEADER section{};
        std::memcpy(&section, executable.data() + table + i * sizeof(section), sizeof(section));
        if (rva < section.VirtualAddress || std::uint64_t{rva} + size > std::uint64_t{section.VirtualAddress} + section.SizeOfRawData) continue;
        const auto offset = std::uint64_t{section.PointerToRawData} + rva - section.VirtualAddress;
        return offset + size <= executable.size() ? executable.data() + offset : nullptr;
    }
    return nullptr;
}
bool codeMatches(ps::CodeIdentityFailure* detail = nullptr) noexcept {
    ps::CodeIdentityFailure local{};
    auto& failure = detail ? *detail : local;
    failure = {}; failure.function = "codeMatches";
    std::uint32_t table = 0;
    std::array<std::byte, 512> headers{};
    if (executable.size() < headers.size()) return ps::identityRefuse(failure, "disk_mapping_failed", ps::imageBase, headers.size());
    if (!ps::identityRead(failure, 0x408B25, &table, sizeof(table))) return false;
    if (table != ps::streamingTable) return ps::identityRefuse(failure, "value_mismatch", 0x408B25, sizeof(table));
    if (!ps::identityRead(failure, ps::imageBase, headers.data(), headers.size()) ||
        !ps::identityBytes(failure, ps::imageBase, headers.data(), executable.data(), headers.size())) return false;
    for (const auto region : ps::codeRegions) {
        const auto* original = diskBytes(region.address, region.size);
        std::array<std::byte, 0x5C0> observed{};
        if (!original) return ps::identityRefuse(failure, "disk_mapping_failed", region.address, region.size);
        if (region.size > observed.size()) return ps::identityRefuse(failure, "buffer_capacity", region.address, region.size);
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
        std::array<std::byte, 0x5C0> reconciled{};
        std::memcpy(reconciled.data(), original, region.size);
        if (!backgroundPatches.overlay(region.address, reconciled.data(), region.size))
            return ps::identityRefuse(failure, "background_disk_bytes_mismatch", region.address, region.size);
        original = reconciled.data(); // Only the exact published sites may differ.
#endif
        if (!identityCodePage(failure, region.address, region.size) ||
            !ps::identityRead(failure, region.address, observed.data(), region.size) ||
            !ps::identityBytes(failure, region.address, observed.data(), original, region.size)) return false;
    }
    ps::CallBytes observed{};
    const auto* disk = diskBytes(ps::phaseCall, observed.size());
    if (!disk) return ps::identityRefuse(failure, "disk_mapping_failed", ps::phaseCall, observed.size());
    if (!ps::identityBytes(failure, ps::phaseCall, disk, ps::originalCall.data(), observed.size(), "disk_bytes_mismatch") ||
        !identityCodePage(failure, ps::phaseCall, observed.size()) ||
        !ps::identityRead(failure, ps::phaseCall, observed.data(), observed.size())) return false;
    // The installed five bytes are intentionally NOT compared with the disk CALL.
    return ps::identityPublishedBytes(failure, ps::phaseCall, observed.data(), ps::originalCall.data(),
        installedCall.data(), observed.size(), hookPublished.load());
}
bool admitExecutable(ps::AdmissionFailure* detail = nullptr) {
    ps::AdmissionFailure local{};
    auto& failure = detail ? *detail : local;
    failure = {};
    hoodlumIdentityAuthenticated.store(false, std::memory_order_release);
    const auto canonical = [&](const wchar_t* input, std::filesystem::path& output, const char* stage) {
        try { output = ps::canonicalForAdmission(input); return true; }
        catch (const std::filesystem::filesystem_error& error) {
            failure.filesystemError = error.code().value();
            failure.filesystemSystem = error.code().category() == std::system_category();
            return ps::admissionRefuse(failure, stage, "canonical_failed");
        }
    };
    const auto imageBaseMatches = [&] {
        const auto base = GetModuleHandleW(nullptr);
        const DWORD error = base ? ERROR_SUCCESS : GetLastError();
        if (reinterpret_cast<std::uintptr_t>(base) != ps::imageBase)
            return ps::admissionRefuse(failure, "image_base", base ? "base_mismatch" : "module_lookup_failed", error);
        return true;
    };
    wchar_t path[32768]{};
    wchar_t ownPath[32768]{};
    const DWORD ownLength = GetModuleFileNameW(module, ownPath, 32768);
    const DWORD ownError = (!ownLength || ownLength >= 32768) ? GetLastError() : ERROR_SUCCESS;
    if (!ownLength || ownLength >= 32768)
        return ps::admissionRefuse(failure, "asi_module_path", ownLength ? "path_truncated" : "path_read_failed", ownError);
#if defined(GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD) && GRANDMP_NATIVE_LOCAL_CAPTURE_BUILD == 1
    // Original two explicit roots; no case folding, aliases or fallback admission.
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    const DWORD pathError = (!length || length >= 32768) ? GetLastError() : ERROR_SUCCESS;
    if (!length || length >= 32768)
        return ps::admissionRefuse(failure, "game_module_path", length ? "path_truncated" : "path_read_failed", pathError);
    if (!imageBaseMatches()) return false;
    std::filesystem::path own, game;
    if (!canonical(ownPath, own, "asi_canonical") || !canonical(path, game, "game_canonical")) return false;
    bool matched = false, ownMatched = false, gameMatched = false;
    for (const auto* root : {L"D:/Jogos/GrandMP-Smoke", L"D:/Jogos/GrandMP-Smoke-B"}) {
        const std::filesystem::path base(root);
        const bool gameHere = game == base / L"gta_sa.exe";
        const bool ownHere = own == base / L"scripts/GrandMPNativePlayerSyncExperimental.asi";
        matched = matched || (gameHere && ownHere);
        gameMatched = gameMatched || gameHere;
        ownMatched = ownMatched || ownHere;
    }
    if (!matched) return ps::admissionRefuse(failure, "canonical_paths",
        !gameMatched ? "game_path_mismatch" : !ownMatched ? "asi_path_mismatch" : "root_pair_mismatch");
#else
    std::filesystem::path own;
    if (!canonical(ownPath, own, "asi_canonical")) return false;
    if (own != std::filesystem::path(
#if defined(GRANDMP_CPLAYER_VARIANT)
            L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPNativePlayerExperimental.asi"
#elif defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
            L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPRemotePedMoveExperimental.asi"
#elif defined(GRANDMP_REMOTE_PED_VARIANT)
            L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPRemotePedExperimental.asi"
#else
            L"D:/Jogos/GrandMP-Smoke/scripts/GrandMPPedSmoke.asi"
#endif
        )) return ps::admissionRefuse(failure, "canonical_paths", "asi_path_mismatch");
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    const DWORD pathError = (!length || length >= 32768) ? GetLastError() : ERROR_SUCCESS;
    if (!length || length >= 32768)
        return ps::admissionRefuse(failure, "game_module_path", length ? "path_truncated" : "path_read_failed", pathError);
    if (!imageBaseMatches()) return false;
    std::filesystem::path game;
    if (!canonical(path, game, "game_canonical")) return false;
    if (game != std::filesystem::path(L"D:/Jogos/GrandMP-Smoke/gta_sa.exe"))
        return ps::admissionRefuse(failure, "canonical_paths", "game_path_mismatch");
#endif
    executableFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (executableFile == INVALID_HANDLE_VALUE)
        return ps::admissionRefuse(failure, "executable_open", "open_failed", GetLastError());
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(executableFile, &size))
        return ps::admissionRefuse(failure, "executable_size", "size_read_failed", GetLastError());
    if (size.QuadPart != 14383616) return ps::admissionRefuse(failure, "executable_size", "size_mismatch");
    executable.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD count = 0;
    const BOOL readOkay = ReadFile(executableFile, executable.data(), static_cast<DWORD>(executable.size()), &count, nullptr);
    const DWORD readError = readOkay ? ERROR_SUCCESS : GetLastError();
    if (!readOkay || count != executable.size()) {
        failure.requested = static_cast<DWORD>(executable.size()); failure.transferred = count;
        return ps::admissionRefuse(failure, "executable_read", readOkay ? "read_incomplete" : "read_failed", readError);
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    const auto opened = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (!BCRYPT_SUCCESS(opened)) {
        failure.status = static_cast<unsigned long>(opened);
        return ps::admissionRefuse(failure, "sha256_provider", "bcrypt_failed");
    }
    std::array<std::byte, 32> digest{};
    const auto result = BCryptHash(algorithm, nullptr, 0, reinterpret_cast<PUCHAR>(executable.data()),
        static_cast<ULONG>(executable.size()), reinterpret_cast<PUCHAR>(digest.data()), static_cast<ULONG>(digest.size()));
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!BCRYPT_SUCCESS(result)) {
        failure.status = static_cast<unsigned long>(result);
        return ps::admissionRefuse(failure, "sha256", "bcrypt_failed");
    }
    if (grandmp::windows::recognizeGameVersion(grandmp::windows::inspectExecutable(executable), digest) !=
        grandmp::windows::GameVersion::gta_sa_10us_hoodlum)
        return ps::admissionRefuse(failure, "executable_identity", "hoodlum_profile_mismatch");
    hoodlumIdentityAuthenticated.store(true, std::memory_order_release);
    if (!codeMatches(&failure.code)) return ps::admissionRefuse(failure, "live_code", failure.code.category, failure.code.error);
    return true;
}
void logAdmissionFailure(const ps::AdmissionFailure& failure) noexcept {
    char detail[352]{};
    if (ps::formatAdmissionFailure(detail, sizeof(detail), failure)) writeLog("failed", detail);
    else writeLog("failed", "admission_diagnostic_format_failed");
    if (std::strcmp(failure.code.category, "none")) {
        if (ps::formatCodeIdentityFailure(detail, sizeof(detail), failure.code)) writeLog("failed", detail);
        else writeLog("failed", "admission_code_diagnostic_format_failed");
    }
}

bool playerReady(void*& ped, ps::Position* position = nullptr, ps::Position* right = nullptr, bool diagnose = false) noexcept {
    auto field = [&](std::uintptr_t address, auto& value, const char* name) {
        const bool okay = read(address, value);
        if (!okay && diagnose) writeSpawnLog("player_ready_read_failed field=%s address=%08lX", name, static_cast<unsigned long>(address));
        return okay;
    };
    auto require = [&](bool okay, const char* name, double observed) {
        if (!okay && diagnose) writeSpawnLog("player_ready_rejected field=%s observed=%.9g", name, observed);
        return okay;
    };
    unsigned char index = 0, paused = 0, userPaused = 0, area = 0;
    int streaming = 0;
    void* pool = nullptr;
    std::uintptr_t vtable = 0, matrix = 0, intelligence = 0, data = 0;
    float health = 0;
    int type = -1;
    if (!field(ps::playerIndex, index, "player_index") || !require(index == 0, "player_index", index) ||
        !field(ps::player, ped, "player_pointer") || !require(ped != nullptr, "player_pointer", reinterpret_cast<std::uintptr_t>(ped)) ||
        !field(ps::codePause, paused, "code_pause") || !require(!paused, "code_pause", paused) ||
        !field(ps::userPause, userPaused, "user_pause") || !require(!userPaused, "user_pause", userPaused) ||
        !field(ps::streamingInitialized, streaming, "streaming_initialized") || !require(streaming == 1, "streaming_initialized", streaming) ||
        !field(ps::pedPool, pool, "ped_pool") || !require(pool != nullptr, "ped_pool", reinterpret_cast<std::uintptr_t>(pool))) return false;
    const auto address = reinterpret_cast<std::uintptr_t>(ped);
    if (!field(address, vtable, "vtable") || !require(vtable == ps::playerVtable, "vtable", vtable) ||
        !field(address + 0x2F, area, "area") || !require(area == 0, "area", area) ||
        !field(address + 0x47C, intelligence, "intelligence") || !require(intelligence != 0, "intelligence", intelligence) ||
        !field(address + 0x480, data, "player_data") || !require(data != 0, "player_data", data) ||
        !field(address + 0x540, health, "health") || !require(std::isfinite(health), "health_nonfinite", health) ||
        !require(health > 0, "health_positive", health) || !field(address + 0x598, type, "ped_type") || !require(type == 0, "ped_type", type) ||
        !field(address + 0x14, matrix, "matrix") || !require(matrix != 0, "matrix", matrix)) return false;
    if (position) {
        if (!field(matrix + 0x30, *position, "position")) return false;
        if (diagnose && !ps::finite(*position))
            writeSpawnLog("cj_position x=%.9g y=%.9g z=%.9g", position->x, position->y, position->z);
        if (!require(std::isfinite(position->x), "position_x_nonfinite", position->x) ||
            !require(std::isfinite(position->y), "position_y_nonfinite", position->y) ||
            !require(std::isfinite(position->z), "position_z_nonfinite", position->z)) return false;
    }
    if (right && !field(matrix, *right, "right")) return false;
    if (diagnose) writeSpawnLog("player_ready_ok cj=%08lX matrix=%08lX", static_cast<unsigned long>(address), static_cast<unsigned long>(matrix));
    return true;
}

bool streamingReady() noexcept {
    unsigned char disabled = 1, scene = 1, bigModel = 1;
    return read(ps::streamingDisabled, disabled) && !disabled &&
        read(ps::loadingScene, scene) && !scene && read(ps::loadingBigModel, bigModel) && !bigModel;
}

#if !defined(GRANDMP_CPLAYER_VARIANT)
// Shared by assignment and the CLOSED diagnostic provider; no new code ranges.
bool taskCodeMatches() noexcept {
    for (const auto region : ps::movement_native::regions) {
        const auto* disk = diskBytes(region.address, region.size);
        std::array<std::byte, 0x450> observed{};
        SIZE_T count = 0;
        if (!disk || region.size > observed.size() || !imagePage(region.address, region.size, region.code) ||
            !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(region.address), observed.data(), region.size, &count) ||
            count != region.size || std::memcmp(disk, observed.data(), region.size)) return false;
    }
    return true;
}

#endif

bool phaseThreadMatches(std::uintptr_t caller) noexcept {
    return gameThread && GetCurrentThreadId() == gameThread && caller == ps::phaseCall + 5;
}

struct SpawnProbe {
    void* cj = nullptr;
    template<class... Args> void trace(const char* format, Args... args) { writeSpawnLog(format, args...); }
    bool player(ps::Position& origin, ps::Position& right) { return playerReady(cj, &origin, &right, true); }
    float ground(float x, float y, float z, bool& found) {
        return reinterpret_cast<float(__cdecl*)(float, float, float, bool*, void**)>(ps::findGround)(x, y, z, &found, nullptr);
    }
    std::uintptr_t sphere(ps::Position target) {
        trace("sphere_query x=%.9g y=%.9g z=%.9g radius=0.35 ignore_cj=%08lX buildings=1 vehicles=1 peds=1 objects=1 dummies=1 camera=0",
            target.x, target.y, target.z, static_cast<unsigned long>(reinterpret_cast<std::uintptr_t>(cj)));
        using Sphere = void*(__cdecl*)(ps::Position, float, void*, bool, bool, bool, bool, bool, bool);
        const auto hit = reinterpret_cast<std::uintptr_t>(reinterpret_cast<Sphere>(ps::testSphere)(target, 0.35f, cj, true, true, true, true, true, false));
        trace("sphere_result hit=%08lX", static_cast<unsigned long>(hit));
        return hit;
    }
};

#if !defined(GRANDMP_CPLAYER_VARIANT)
struct NativeEngine {
    void* owned = nullptr; // Stable storage registered with the engine reference system.
    std::uintptr_t creationResult = 0; // Keep the exact AddPed result even if its registered reference is cleared.
    int reference = -1;
    const char* ownershipFailure = "not_observed";
    void log(ps::State state, const char* reason) {
        if (state == ps::State::ownership_lost) {
            char detail[192]{};
            std::snprintf(detail, sizeof(detail), "%s detail=%s", reason, ownershipFailure);
            writeLog(ps::name(state), detail, creationResult); // Raw result is diagnostic only, never dereferenced here.
        } else writeLog(ps::name(state), reason, creationResult);
    }
    bool requestModel() {
        reinterpret_cast<void(__cdecl*)(int, int)>(ps::requestModel)(ps::male01, 0); // DEFAULT, no global ownership flags.
        reinterpret_cast<void(__cdecl*)(bool)>(ps::loadModels)(false);
        return true;
    }
    bool modelLoaded() {
        unsigned char state = 0;
        return read(ps::streamingTable + ps::male01 * 0x14 + 0x10, state) && state == 1;
    }
    bool revalidateModel() {
        unsigned char state = 0;
        const bool okay = read(ps::streamingTable + ps::male01 * 0x14 + 0x10, state);
        writeSpawnLog("model_revalidate model=%u read_ok=%u load_state=%u loaded=%u", ps::male01, unsigned(okay), unsigned(state), unsigned(okay && state == 1));
        return okay && state == 1;
    }
    bool spawnPosition(ps::Position& target) {
        SpawnProbe probe;
        return ps::findSpawnPosition(probe, target);
    }
    bool registered() const noexcept {
        if (!owned) return false;
        std::uintptr_t link = 0;
        if (!read(reinterpret_cast<std::uintptr_t>(owned) + 0x24, link)) return false;
        for (unsigned i = 0; link && i < 128; ++i) {
            std::uintptr_t destination = 0, next = 0;
            if (!read(link + 4, destination) || !read(link, next)) return false;
            if (destination == reinterpret_cast<std::uintptr_t>(&owned)) return true;
            link = next;
        }
        return false;
    }
    std::uintptr_t create(const ps::Position& position) {
        using Add = void*(__cdecl*)(int, unsigned, const ps::Position&, bool);
        owned = reinterpret_cast<Add>(ps::addPed)(4, ps::male01, position, false);
        creationResult = reinterpret_cast<std::uintptr_t>(owned);
        if (!owned) return creationResult;
        using Register = void(__thiscall*)(void*, void**);
        reinterpret_cast<Register>(ps::registerReference)(owned, &owned);
        if (owned) reference = reinterpret_cast<int(__cdecl*)(void*)>(ps::pedRef)(owned);
        return creationResult;
    }
    bool owns(std::uintptr_t ped) {
        auto refuse = [&](const char* reason) { ownershipFailure = reason; return false; };
        if (!owned) return refuse("registered_pointer_null_observed");
        if (reinterpret_cast<std::uintptr_t>(owned) != ped) return refuse("tracked_pointer_mismatch");
        void* cj = nullptr;
        if (!read(ps::player, cj) || owned == cj) return refuse("cj_identity_unavailable_or_equal");
        if (reference < 0) return refuse("pool_reference_invalid");
        // Hoodlum GetPed (54FF90) does not bounds-check the slot. CPool layout:
        // objects +0, byte map +4, size +8 (plugin-sdk 15f15b60 CPool.h).
        std::uintptr_t pool = 0, objects = 0, flags = 0;
        int size = 0;
        unsigned char flag = 0;
        const auto index = static_cast<unsigned>(reference) >> 8;
        if (!read(ps::pedPool, pool) || !pool || !read(pool, objects) || !objects ||
            !read(pool + 4, flags) || !flags || !read(pool + 8, size) || size <= 0 || index >= static_cast<unsigned>(size))
            return refuse("pool_or_slot_unavailable");
        if (!read(flags + index, flag) || (flag & 0x80U) || flag != (reference & 0xFF))
            return refuse("pool_slot_unreadable_free_or_generation_changed");
        if (reinterpret_cast<void*(__cdecl*)(int)>(ps::resolvePed)(reference) != owned)
            return refuse("pool_handle_identity_mismatch");
        // Only now may the reference list of a live, pool-authenticated object be read.
        if (!registered()) return refuse("registered_link_not_proven");
        return true;
    }
    bool visible(std::uintptr_t ped) {
        std::uintptr_t clump = 0;
        unsigned short model = 0;
        std::uint32_t flags = 0;
        return read(ped + 0x18, clump) && clump && read(ped + 0x22, model) && model == ps::male01 &&
            read(ped + 0x1C, flags) && (flags & 0x80U); // CEntity::bIsVisible, not a screen-space proof.
    }
    void moveLog(const char* state, const char* reason) { writeLog(state, reason, creationResult); }
    const char* movePosition(std::uintptr_t ped, ps::Position& position) {
        // Authenticate the pool/reference before inspecting any entity fields.
        if (!owns(ped)) return "ownership_unproven";
        void* cj = nullptr;
        ps::Position playerPosition{};
        std::uintptr_t matrix = 0;
        float health = 0;
        int type = -1;
        unsigned char area = 0;
        std::uint32_t flags = 0;
        if (!playerReady(cj, &playerPosition) || !streamingReady()) return "gameplay_unavailable";
        if (!visible(ped) || !read(ped + 0x14, matrix) || !matrix || !read(matrix + 0x30, position) ||
            !read(ped + 0x540, health) || !read(ped + 0x598, type) || !read(ped + 0x2F, area) || !read(ped + 0x46C, flags))
            return "ped_pose_or_state_unavailable";
        if (!ps::finite(position) || !std::isfinite(health)) return "ped_pose_or_health_nonfinite";
        if (health <= 0 || type != 4 || area != 0 || (flags & 0x100U)) return "ped_dead_wrong_type_area_or_vehicle";
        if (ps::distance2D(position, playerPosition) > 12.f || std::abs(position.z - playerPosition.z) > 3.f)
            return "cj_ped_proximity_limit";
        return nullptr;
    }
    const char* moveDestination(std::uintptr_t ped, ps::Position origin, ps::Position& destination) {
        if (!owns(ped)) return "ownership_unproven";
        std::uintptr_t matrix = 0;
        ps::Position forward{};
        if (!read(ped + 0x14, matrix) || !matrix || !read(matrix + 0x10, forward)) return "ped_forward_unavailable";
        struct Probe {
            void* ignore;
            float ground(float x, float y, float z, bool& found) {
                return reinterpret_cast<float(__cdecl*)(float, float, float, bool*, void**)>(ps::findGround)(x, y, z, &found, nullptr);
            }
            std::uintptr_t sphere(ps::Position center) {
                using Sphere = void*(__cdecl*)(ps::Position, float, void*, bool, bool, bool, bool, bool, bool);
                return reinterpret_cast<std::uintptr_t>(reinterpret_cast<Sphere>(ps::testSphere)(center, 0.35f, ignore, true, true, true, true, true, false));
            }
        } probe{owned};
        const char* failure = ps::findMoveDestination(probe, origin, forward, destination);
        if (!failure) {
            void* cj = nullptr;
            ps::Position playerPosition{};
            if (!playerReady(cj, &playerPosition)) return "destination_player_unavailable";
            if (ps::distance2D(destination, playerPosition) > 12.f || std::abs(destination.z - playerPosition.z) > 3.f)
                return "destination_cj_proximity_limit";
            char detail[192]{};
            std::snprintf(detail, sizeof(detail), "origin=(%.3f,%.3f,%.3f) target=(%.3f,%.3f,%.3f) distance=4 walk=4 slot=3",
                origin.x, origin.y, origin.z, destination.x, destination.y, destination.z);
            moveLog("move_probe", detail);
        }
        return failure;
    }
    const char* assignMove(std::uintptr_t ped, const ps::Position& destination) {
        namespace mn = ps::movement_native;
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        if (!grandmp::windows::remote_ped_move::physicalExecutionApproved) return "remote_movement_physical_review_missing";
#else
        if (!ps::movementExecutionApproved) return "movement_physical_review_missing";
#endif
        if (GetCurrentThreadId() != gameThread || !owns(ped)) return "thread_or_ownership_unproven";
        // The existing executable identity/CALL gates remain intact. Add read-only
        // comparisons for new task code and its vtable before any allocation.
        if (!codeMatches()) return "live_code_changed";
        if (!taskCodeMatches()) return "task_code_or_vtable_changed";
        std::uintptr_t intelligence = 0, intelligencePed = 0, managerPed = 0;
        if (!read(ped + ps::task_layout::intelligence, intelligence) || !intelligence || !read(intelligence, intelligencePed) || intelligencePed != ped ||
            !read(intelligence + ps::task_layout::manager + ps::task_layout::managerOwner, managerPed) || managerPed != ped) return "intelligence_manager_owner_mismatch";
        const auto manager = intelligence + ps::task_layout::manager;
        // Never replace a primary task or override physical/event responses.
        for (unsigned i = 0; i <= mn::primarySlot; ++i) {
            std::uintptr_t task = 0;
            if (!read(manager + i * 4, task) || task) return "primary_task_slot_busy_or_unreadable";
        }
        std::uintptr_t pool = 0, objects = 0, flags = 0;
        int size = 0;
        if (!read(mn::taskPool, pool) || !pool || !read(pool, objects) || !objects || !read(pool + 4, flags) || !flags ||
            !read(pool + 8, size) || size <= 0 || size > 10000) return "task_pool_unavailable";
        unsigned free = 0;
        for (int i = 0; i < size && free < 8; ++i) {
            unsigned char flag = 0;
            if (!read(flags + i, flag)) return "task_pool_map_unreadable";
            free += (flag & 0x80U) != 0;
        }
        if (free < 8) return "task_pool_capacity_low";
        void* memory = reinterpret_cast<mn::Allocate>(mn::allocateTask)(mn::taskSize);
        if (!memory) return "task_allocation_null";
        const auto address = reinterpret_cast<std::uintptr_t>(memory);
        unsigned char flag = 0;
        if (address < objects || (address - objects) % mn::taskStride || (address - objects) / mn::taskStride >= static_cast<unsigned>(size) ||
            !read(flags + (address - objects) / mn::taskStride, flag) || (flag & 0x80U))
            return "task_allocation_identity_unknown_retained_no_delete";
        void* task = mn::construct(reinterpret_cast<mn::Construct>(mn::constructWalk), memory, destination);
        std::uintptr_t vtable = 0;
        if (task != memory || !read(address, vtable) || vtable != mn::walkVtable)
            return "task_constructor_identity_unknown_retained_no_delete";
        mn::transfer(reinterpret_cast<mn::SetTask>(mn::setTask), reinterpret_cast<void*>(manager), task);
        // SetTask owns (and may already have deleted) the task. Compare only the
        // manager slot; never dereference/free the transferred pointer on failure.
        if (!owns(ped)) return "post_transfer_ownership_unproven_no_task_delete";
        std::uintptr_t installed = 0;
        if (!read(manager + mn::primarySlot * 4, installed) || installed != address)
            return "task_not_retained_by_manager_no_retry_no_delete";
        return nullptr;
    }
    bool remove(std::uintptr_t ped) {
        if (!owns(ped)) return false;
        void* victim = owned;
        reinterpret_cast<void(__thiscall*)(void*, void**)>(ps::cleanReference)(victim, &owned);
        owned = nullptr;
        reference = -1;
        reinterpret_cast<void(__cdecl*)(void*)>(ps::removePed)(victim); // CWorld::Remove + virtual deleting destructor.
        return true;
    }
} engine;

#endif // Legacy civil experiment
#if defined(GRANDMP_CPLAYER_VARIANT)
#include "native_player_runtime.inl"
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
#include "background_gameplay_runtime.inl"
#endif
#endif

#if defined(GRANDMP_REMOTE_PED_VARIANT)
#include "remote_ped_runtime.inl"
#endif

#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
#include "native_diagnostic_windows_runtime.inl"
#include "native_diagnostic_live.inl"
#endif

void runPhase(std::uintptr_t caller) noexcept {
    if (callbacks.fetch_add(1, std::memory_order_relaxed) == 0) writeLog("waiting", "callback_executed");
    if (preparation.load() != 3) return; // Do not spawn while publication/protection checks are still in progress.
    if (!phaseThreadMatches(caller)) {
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
        bg::close(); // Do not touch engine-thread diagnostic state from an unknown caller.
#endif
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
        stopTaskDiagnostic();
#endif
        runtimeDisabled.store(true); writeLog("failed", "wrong_phase_caller_or_thread"); return;
    }
    if (runtimeDisabled.load()) {
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
        bg::close(); backgroundReport();
#endif
#if defined(GRANDMP_NATIVE_INPUT_REPLAY_BUILD) && GRANDMP_NATIVE_INPUT_REPLAY_BUILD == 1
        inputReport(); // Deferred until the existing admitted phase; never in the control wrapper.
#endif
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
        stopTaskDiagnostic();
#endif
#if defined(GRANDMP_CPLAYER_VARIANT)
        nativeFinished=true;
#elif defined(GRANDMP_REMOTE_PED_VARIANT)
        remoteController.disable();
#else
        experiment.fail();
#endif
        return;
    }
    // Also guard validation/native queries against any nested engine dispatch.
    if (phaseDispatchActive) return;
    phaseDispatchActive = true;
    struct Leave { bool& value; ~Leave() { value = false; } } leave{phaseDispatchActive};
#if defined(GRANDMP_CPLAYER_VARIANT)
    ps::CodeIdentityFailure commonFailure{}, factoryFailure{};
    const bool commonMatches = codeMatches(&commonFailure);
    const bool factoryMatches = factoryCodeMatches(&factoryFailure); // Evaluate independently, even if common failed.
    if (!commonMatches || !factoryMatches) {
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
        backgroundStop("phase_code_identity");
#endif
        runtimeDisabled.store(true); // Close dispatch BEFORE emitting the one terminal diagnostic.
        writeLog("failed", commonMatches ? "codeMatches=pass factoryCodeMatches=fail" :
            factoryMatches ? "codeMatches=fail factoryCodeMatches=pass" : "codeMatches=fail factoryCodeMatches=fail");
        const auto report = [](const ps::CodeIdentityFailure& failure) {
            char detail[320]{}; // Final writeLog line remains within its 512-byte buffer.
            if (ps::formatCodeIdentityFailure(detail, sizeof(detail), failure)) writeLog("failed", detail);
            else writeLog("failed", "identity_diagnostic_format_failed");
        };
        if (!commonMatches) report(commonFailure);
        if (!factoryMatches) report(factoryFailure);
        writeLog("failed", "native_player_code_identity_terminal_no_cleanup");
        return; // No factory/engine operation, cleanup, retry or subsequent frame logging.
    }
#endif
#if defined(GRANDMP_REMOTE_PED_VARIANT)
    // Fail permanently even outside gameplay; never clean up unknown identity.
    if (!codeMatches()) {
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
        stopTaskDiagnostic();
#endif
        runtimeDisabled.store(true);
        remoteController.disable();
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
        remoteEngine.moveLog("remote_move_failed", "native_code_identity_terminal_no_cleanup");
#endif
        writeLog("failed", "live_code_changed_remote_terminal_no_cleanup");
        return;
    }
#endif
    void* cj = nullptr;
    const bool gameplay = playerReady(cj) && streamingReady();
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
    backgroundReport(); // Deferred: no logging inside UpdatePads or WndProc.
#endif
#if defined(GRANDMP_CPLAYER_VARIANT)
    nativeTick(gameplay);
#elif defined(GRANDMP_REMOTE_PED_VARIANT)
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
    TaskDiagnosticFrame diagnostic(caller,callbacks.load(std::memory_order_relaxed));
    if (rp::physicalExecutionApproved && remoteReceiver.load() == rp::ReceiverState::ready)
        remoteController.tick(remoteEngine, remoteIntents.snapshot(), true, gameplay, GetTickCount64(), diagnostic);
    diagnostic.afterTick(remoteController); // Also after arrived, when Controller no longer advances Movement.
#else
    if (rp::physicalExecutionApproved && remoteReceiver.load() == rp::ReceiverState::ready)
        remoteController.tick(remoteEngine, remoteIntents.snapshot(), true, gameplay, GetTickCount64());
#endif
#else
    if (gameplay && !codeMatches()) { experiment.fail(); writeLog("failed", "live_code_changed"); return; }
    DWORD foregroundPid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &foregroundPid);
    static ps::PressEdge f8, f7;
    if (f8.sample((GetAsyncKeyState(VK_F8) & 0x8000) != 0, gameplay && foregroundPid == GetCurrentProcessId())) removalRequested.store(true);
    const bool remove = gameplay && removalRequested.exchange(false);
    const bool move = f7.sample((GetAsyncKeyState(VK_F7) & 0x8000) != 0, gameplay && foregroundPid == GetCurrentProcessId());
    const auto now = GetTickCount64();
    experiment.tick(engine, true, gameplay, remove, now);
    movement.tick(engine, experiment, true, gameplay, move, remove, now);
#endif
}

DWORD engineWindowThread() noexcept {
    std::uintptr_t platform = 0;
    HWND window = nullptr;
    DWORD pid = 0;
    if (!read(ps::platformGlobal, platform) || !platform || !read(platform, window) ||
        !window || !IsWindow(window) || !IsWindowVisible(window)) return 0;
    const DWORD thread = GetWindowThreadProcessId(window, &pid);
    return pid == GetCurrentProcessId() ? thread : 0;
}

void storeCandidateOutcome(const ps::InstallOutcome& outcome) noexcept {
    installCandidateFailure.store(static_cast<std::uint32_t>(outcome.primary.failure));
    installCandidateError.store(outcome.primary.win32Error);
    installCandidateHasError.store(outcome.primary.hasWin32Error);
    installCandidateCleanupFailure.store(static_cast<std::uint32_t>(outcome.candidateCleanup.failure));
    installCandidateCleanupError.store(outcome.candidateCleanup.win32Error);
    installCandidateCleanupHasError.store(outcome.candidateCleanup.hasWin32Error);
}

ps::InstallOutcome loadCandidateOutcome() noexcept {
    ps::InstallOutcome outcome{};
    outcome.primary.failure = static_cast<ps::InstallFailure>(installCandidateFailure.load());
    outcome.primary.win32Error = installCandidateError.load();
    outcome.primary.hasWin32Error = installCandidateHasError.load();
    outcome.candidateCleanup.failure = static_cast<ps::InstallFailure>(installCandidateCleanupFailure.load());
    outcome.candidateCleanup.win32Error = installCandidateCleanupError.load();
    outcome.candidateCleanup.hasWin32Error = installCandidateCleanupHasError.load();
    return outcome;
}

void logInstallDiagnostic(const ps::InstallDiagnostic& diagnostic) noexcept {
    if (diagnostic.failure == ps::InstallFailure::none) return;
    char reason[128]{};
    if (ps::formatInstallDiagnostic(reason, sizeof(reason), diagnostic)) writeLog("failed", reason);
    else writeLog("failed", "install_diagnostic_format_failed");
}

ps::InstallOutcome installCandidate() {
    const auto fail = [](ps::InstallFailure failure) {
        ps::InstallOutcome result{};
        result.primary.failure = failure;
        return result;
    };
    const auto win32Fail = [](ps::InstallFailure failure, DWORD error) {
        ps::InstallOutcome result{};
        result.primary = {failure, error, true};
        return result;
    };
    // The reviewed build cannot enter a writer. No opt-in overrides this gate.
    if (!ps::physicalExecutionApproved) return fail(ps::InstallFailure::candidate_physical_gate_closed);
    void* cj = nullptr;
    // A five-byte CALL is not atomic. Install only on the engine/window thread
    // while it is in GetMessage/PeekMessage, never from the preparation worker.
    int cpu[4]{};
    __cpuid(cpu, 1);
    if (!gameThread) return fail(ps::InstallFailure::candidate_game_thread_unavailable);
    if (engineWindowThread() != gameThread) return fail(ps::InstallFailure::candidate_window_thread_mismatch);
    if (GetCurrentThreadId() != gameThread) return fail(ps::InstallFailure::candidate_wrong_thread);
    if (!(cpu[3] & (1 << 24))) return fail(ps::InstallFailure::candidate_cpu_fxsr_unavailable);
    if (!IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE)) return fail(ps::InstallFailure::candidate_sse2_unavailable);
    if (!playerReady(cj)) return fail(ps::InstallFailure::candidate_player_not_ready);
    if (!streamingReady()) return fail(ps::InstallFailure::candidate_streaming_not_ready);
    if (!codeMatches()) return fail(ps::InstallFailure::candidate_code_identity_mismatch);
    if (!ps::encodePhaseCall(reinterpret_cast<std::uintptr_t>(&ps::phaseThunk), installedCall))
        return fail(ps::InstallFailure::candidate_call_encoding_failed);
    const auto backend = ps::patch_transaction::win32Backend(nullptr, authorizeCowPatch);
    const auto write = ps::patch_transaction::apply(backend, ps::phaseCall,
        std::span<const std::byte>(ps::originalCall), std::span<const std::byte>(installedCall),
        hoodlumIdentityAuthenticated.load(std::memory_order_acquire));
    if (!write) {
        ps::InstallOutcome result{};
        const auto failure = write.failure;
        const auto primary = failure == ps::patch_transaction::Failure::unexpected_protection
            ? ps::InstallFailure::candidate_unexpected_page_protection
            : failure == ps::patch_transaction::Failure::protect_failed
                ? ps::InstallFailure::candidate_virtual_protect_failed
                : failure == ps::patch_transaction::Failure::flush_failed
                    ? ps::InstallFailure::candidate_instruction_cache_flush_failed
                    : failure == ps::patch_transaction::Failure::protection_restore_failed ||
                      failure == ps::patch_transaction::Failure::restore_transition_mismatch ||
                      failure == ps::patch_transaction::Failure::post_restore_page
                        ? ps::InstallFailure::candidate_protection_restore_failed
                        : failure == ps::patch_transaction::Failure::write_failed ||
                          failure == ps::patch_transaction::Failure::write_partial
                            ? ps::InstallFailure::candidate_code_write_failed
                            : ps::InstallFailure::candidate_code_identity_mismatch;
        result.primary = {primary, write.error, write.error != ERROR_SUCCESS};
        if (!write.rollbackComplete && (write.cleanupError != ERROR_SUCCESS || write.bytesChanged || write.rollbackAttempted))
            result.candidateCleanup = {ps::InstallFailure::candidate_code_rollback_failed,
                write.cleanupError ? write.cleanupError : ERROR_INVALID_DATA, true};
        return result;
    }
    hookPublished.store(true, std::memory_order_release);
    if (!codeMatches()) {
        const auto rollback = ps::patch_transaction::apply(backend, ps::phaseCall,
            std::span<const std::byte>(installedCall), std::span<const std::byte>(ps::originalCall),
            hoodlumIdentityAuthenticated.load(std::memory_order_acquire));
        if (rollback) hookPublished.store(false, std::memory_order_release);
        auto result = fail(ps::InstallFailure::candidate_post_publish_code_mismatch);
        if (!rollback) result.candidateCleanup = {ps::InstallFailure::candidate_code_rollback_failed,
            rollback.cleanupError ? rollback.cleanupError : rollback.error, true};
        return result;
    }
    return {};
}

LRESULT CALLBACK installMessage(int code, WPARAM word, LPARAM message) {
    unsigned armed = 1;
    if (code >= 0 && installation.compare_exchange_strong(armed, 2)) {
        const auto outcome = installCandidate();
        storeCandidateOutcome(outcome);
#if defined(GRANDMP_CPLAYER_VARIANT)
        if (outcome.succeeded() && !prepareNativeBinding()) runtimeDisabled.store(true);
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
        if (outcome.succeeded() && !runtimeDisabled.load() && !installBackground()) runtimeDisabled.store(true);
#endif
#endif
        installation.store(3);
    }
    return CallNextHookEx(nullptr, code, word, message);
}

ps::InstallOutcome installOnGameThread() {
    ps::InstallOutcome outcome{};
    if (!ps::physicalExecutionApproved) {
        outcome.primary.failure = ps::InstallFailure::candidate_physical_gate_closed;
        return outcome;
    }
    if (!gameThread) {
        outcome.primary.failure = ps::InstallFailure::window_thread_unavailable;
        return outcome;
    }
    HMODULE pinned = nullptr;
    const BOOL pinSucceeded = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&ps::phaseThunk), &pinned);
    const DWORD pinError = pinSucceeded ? ERROR_SUCCESS : GetLastError();
    if (!pinSucceeded) {
        outcome.primary = {ps::InstallFailure::module_pin_failed, pinError, true};
        return outcome;
    }
    if (pinned != module) {
        outcome.primary.failure = ps::InstallFailure::module_pin_mismatch;
        return outcome;
    }
    installation.store(1);
    // This bootstrap does no engine work. Its sole job is same-thread publication.
    const HHOOK hook = SetWindowsHookExW(WH_GETMESSAGE, installMessage, module, gameThread);
    const DWORD hookError = hook ? ERROR_SUCCESS : GetLastError();
    if (!hook) {
        installation.store(4);
        outcome.primary = {ps::InstallFailure::set_windows_hook_failed, hookError, true};
        return outcome;
    }
    const bool posted = PostThreadMessageW(gameThread, WM_NULL, 0, 0) != FALSE;
    const DWORD postError = posted ? ERROR_SUCCESS : GetLastError();
    if (!posted) outcome.bootstrapWarning = {ps::InstallFailure::post_thread_message_failed, postError, true};
    const auto deadline = GetTickCount64() + 5000;
    while (posted && installation.load() == 1 && GetTickCount64() < deadline) Sleep(1);
    unsigned armed = 1;
    installation.compare_exchange_strong(armed, 4); // Cancel only if not yet executing.
    while (installation.load() == 2) Sleep(1); // Do not close identity storage under the installer.
    const unsigned installationState = installation.load();
    if (installationState == 3) outcome.primary = loadCandidateOutcome().primary;
    else if (posted) outcome.primary.failure = ps::InstallFailure::callback_timeout_5000ms;
    else if (outcome.primary.failure == ps::InstallFailure::none)
        outcome.primary = outcome.bootstrapWarning;
    const BOOL unhooked = UnhookWindowsHookEx(hook);
    const DWORD unhookError = unhooked ? ERROR_SUCCESS : GetLastError();
    if (!unhooked) {
        runtimeDisabled.store(true);
        outcome.unhookFailure = {ps::InstallFailure::unhook_failed, unhookError, true};
    }
    if (installationState == 3) {
        const auto candidate = loadCandidateOutcome();
        outcome.primary = candidate.primary;
        outcome.candidateCleanup = candidate.candidateCleanup;
    }
    return outcome; // Worker opens dispatch only after bootstrap removal.
}

DWORD WINAPI prepare(void* reference) {
    BOOT_NOTE(prepare,entered);
    ps::AdmissionFailure admissionFailure{};
    try {
        writeLog("loaded", "experimental_module_no_engine_calls_in_loader");
        writeLog("waiting", "preparation_read_only");
        if (!ps::physicalExecutionApproved
#if defined(GRANDMP_REMOTE_PED_VARIANT)
            || !rp::physicalExecutionApproved
#if defined(GRANDMP_REMOTE_PED_MOVE_VARIANT)
            || !grandmp::windows::remote_ped_move::physicalExecutionApproved
#endif
#endif
        ) {
            writeLog("waiting", "installation_blocked_physical_execution_review_missing");
            preparation.store(2);
        } else if (!admitExecutable(&admissionFailure)) {
            logAdmissionFailure(admissionFailure);
            writeLog("failed", "hoodlum_identity_path_or_live_signature");
            preparation.store(4);
        } else {
            wchar_t enabled[4]{};
            GetEnvironmentVariableW(L"GRANDMP_PED_SMOKE_ENABLE", enabled, 4);
            #if defined(GRANDMP_CPLAYER_VARIANT)
            wchar_t nativeEnabled[4]{};
            GetEnvironmentVariableW(L"GRANDMP_NATIVE_PLAYER_ENABLE",nativeEnabled,4);
            if (std::wstring_view(nativeEnabled)!=L"1") enabled[0]=0;
#endif
            if (std::wstring_view(enabled) != L"1") {
                writeLog("waiting", "explicit_opt_in_missing");
                preparation.store(2);
            } else {
                writeLog("waiting", "press_F6_in_gameplay_to_install_candidate");
                const auto deadline = GetTickCount64() + 120000;
                bool f6Detected = false;
                ps::InstallOutcome installOutcome{};
                while (GetTickCount64() < deadline) {
                    DWORD pid = 0;
                    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
                    if (pid == GetCurrentProcessId() && (GetAsyncKeyState(VK_F6) & 0x8000)) {
                        f6Detected = true;
                        writeLog("waiting", "installation_f6_detected_attempt_started");
                        gameThread = engineWindowThread();
                        installOutcome = installOnGameThread();
                        break;
                    }
                    Sleep(50);
                }
                if (!f6Detected) installOutcome.primary = ps::f6WaitResult(false);
                logInstallDiagnostic(installOutcome.primary);
                if (installOutcome.bootstrapWarning.failure != ps::InstallFailure::none &&
                    installOutcome.bootstrapWarning.failure != installOutcome.primary.failure) {
                    const auto warning = installOutcome.bootstrapWarning;
                    char detail[128]{};
                    if (ps::formatInstallDiagnostic(detail, sizeof(detail), warning)) writeLog("waiting", detail);
                    else writeLog("failed", "install_diagnostic_format_failed");
                }
                logInstallDiagnostic(installOutcome.candidateCleanup);
                logInstallDiagnostic(installOutcome.unhookFailure);
                bool installed = installOutcome.succeeded();
#if defined(GRANDMP_BACKGROUND_GAMEPLAY_BUILD) && GRANDMP_BACKGROUND_GAMEPLAY_BUILD == 1
                backgroundReport(); // Preparation worker, after bootstrap publication.
#endif
#if defined(GRANDMP_CPLAYER_VARIANT)
                if (installed && (runtimeDisabled.load() || !startNativeNetwork())) { runtimeDisabled.store(true);installed=false; }
#endif
#if defined(GRANDMP_REMOTE_PED_VARIANT)
                if (installed && !startRemoteNetwork()) {
                    runtimeDisabled.store(true);
                    installed = false;
                    writeLog("failed", "remote_worker_start_refused_module_pinned");
                }
#endif
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
                if (installed) prepareTaskDiagnostic(); // Allocate/publish BEFORE admitting callbacks, outside loader lock.
#endif
                preparation.store(installed ? 3 : 4);
                writeLog(installed ? "waiting" : "failed", installed ? "post_population_phase_installed_original_immutable_module_pinned" : "installation_refused");
#if defined(GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD) && GRANDMP_REMOTE_TASK_DIAGNOSTIC_BUILD == 1
                if (installed) serviceTaskDiagnostic(); // Existing preparation worker, bounded wait, no engine calls.
#endif
            }
        }
    } catch (...) { preparation.store(4); writeLog("failed", "preparation_exception_no_retry"); }
    if (!hookPublished.load() && executableFile != INVALID_HANDLE_VALUE) {
        CloseHandle(executableFile);
        executableFile = INVALID_HANDLE_VALUE;
    }
    BOOT_NOTE(prepare_exit,succeeded,0,static_cast<LONG>(preparation.load()));
    FreeLibraryAndExitThread(static_cast<HMODULE>(reference), 0);
}
} // namespace

namespace grandmp::windows::ped_smoke {
const PhaseOriginal originalPhase = reinterpret_cast<PhaseOriginal>(updateWeapons);
void __cdecl phaseBody(std::uintptr_t caller) noexcept { runPhase(caller); }
}

extern "C" void __cdecl InitializeASI() noexcept {
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
    InterlockedIncrement(&GrandMPNativePlayerBootstrapDiagnostic.initializeCalls);
#endif
    unsigned expected = 0;
    if (!preparation.compare_exchange_strong(expected, 1)) return;
    BOOT_NOTE(initialize,entered);
    HMODULE reference = nullptr;
    BOOT_NOTE(reference,entered);
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS, reinterpret_cast<LPCWSTR>(&InitializeASI), &reference)) {
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
        const DWORD error=GetLastError();
        BOOT_NOTE(reference,failed,error);
        BOOT_NOTE(initialize_exit,failed);
#endif
        preparation.store(4);
        return;
    }
    BOOT_NOTE(reference,succeeded,0,reference==module ? 1 : 2);
    BOOT_NOTE(thread,entered);
    const HANDLE worker = CreateThread(nullptr, 0, prepare, reference, 0, nullptr);
    if (!worker) {
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
        const DWORD error=GetLastError();
        BOOT_NOTE(thread,failed,error);
        BOOT_NOTE(initialize_exit,failed);
#endif
        preparation.store(4); FreeLibrary(reference); return;
    }
    BOOT_NOTE(thread,succeeded);
    CloseHandle(worker); // Preparation only. No thread ever drives gameplay.
    BOOT_NOTE(initialize_exit,succeeded);
}
extern "C" unsigned __cdecl GrandMPPedSmokeStatus() noexcept { return preparation.load(); }
extern "C" unsigned __cdecl GrandMPPedSmokeCallbackCount() noexcept { return callbacks.load(); }
extern "C" void __cdecl GrandMPPedSmokeRequestRemoval() noexcept { removalRequested.store(true); }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        module = instance;
#if defined(GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD) && GRANDMP_BOOTSTRAP_DIAGNOSTIC_BUILD == 1
        // Own, constant-initialized DATA only. No I/O, thread, wait or loader API.
        InterlockedExchange(&GrandMPNativePlayerBootstrapDiagnostic.attached,1);
#endif
    }
    return TRUE;
}
