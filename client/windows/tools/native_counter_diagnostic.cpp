#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include "native_counter_preflight.hpp"

#include <cstdio>
#include <cwchar>
#include <cstring>
#include <cerrno>
#include <string_view>

namespace {
using namespace grandmp::windows;
struct Handle {
    HANDLE value;
    ~Handle() { if (value && value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
bool read(HANDLE process, std::uint32_t address, void* data, std::size_t length) {
    SIZE_T got = 0;
    return ReadProcessMemory(process, reinterpret_cast<LPCVOID>(address), data, length, &got) && got == length;
}
const wchar_t* stateName(std::uint32_t state) {
    switch (state) {
    case 0: return L"UNINITIALIZED"; case 1: return L"REQUESTED";
    case 2: return L"READY"; case 3: return L"SHUTDOWN";
    case 4: return L"FAILED"; case 5: return L"INITIALIZING";
    default: return nullptr;
    }
}
}
int wmain(int argc, wchar_t** argv) {
    if ((argc != 5 && argc != 6) || std::wstring_view(argv[1]) != L"--pid" ||
        std::wstring_view(argv[3]) != L"--expected-asi" ||
        (argc == 6 && std::wstring_view(argv[5]) != L"--lab-profile" &&
         std::wstring_view(argv[5]) != L"--dry-run")) {
        std::fwprintf(stderr, L"Usage: --pid decimal --expected-asi file.asi [--lab-profile | --dry-run]\n");
        return 2;
    }
    wchar_t* end = nullptr;
    errno = 0;
    const auto pid = std::wcstoul(argv[2], &end, 10);
    if (errno || !pid || !*argv[2] || *argv[2] == L'-' || *argv[2] == L'+' || *end) return 2;
    try {
        const auto expected = readNativeCounterFile(argv[4]);
        const auto rva = expected ? nativeCounterReadyRva(*expected) : std::nullopt;
        const auto wrapperRva = expected ? nativeCounterWrapperRva(*expected) : std::nullopt;
        if (!expected || !rva || !wrapperRva) { std::puts("INCONCLUSIVO: expected PE32/exports invalid"); return 3; }
        Handle process{OpenProcess(PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid)};
        if (!process.value) { std::printf("INCONCLUSIVO: OpenProcess error=%lu\n", GetLastError()); return 3; }
        wchar_t mainPath[32768]{};
        DWORD length = static_cast<DWORD>(std::size(mainPath));
        if (!QueryFullProcessImageNameW(process.value, 0, mainPath, &length)) return 3;
        const auto image = readNativeCounterFile(mainPath);
        const bool lab = argc == 6 && std::wstring_view(argv[5]) == L"--lab-profile";
        const bool dryRun = argc == 6 && std::wstring_view(argv[5]) == L"--dry-run";
        if (!image || (lab ? std::filesystem::path(mainPath).filename() != L"grandmp_native_counter_host.exe" :
            recognizeGameVersion(image->inspection, image->digest) != GameVersion::gta_sa_10us_hoodlum)) {
            std::puts("INCONCLUSIVO: process identity rejected"); return 3;
        }
        Handle snapshot{CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid)};
        if (snapshot.value == INVALID_HANDLE_VALUE) return 3;
        MODULEENTRY32W entry{sizeof(entry)};
        if (!Module32FirstW(snapshot.value, &entry)) return 3;
        std::uint32_t base = 0;
        unsigned matches = 0;
        bool mainVerified = false;
        do {
            const auto currentBase = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
            if (std::filesystem::path(entry.szExePath) == std::filesystem::path(mainPath)) {
                mainVerified = lab ? image->inspection.status == PeStatus::pe32_i386 :
                    currentBase == 0x400000 && image->imageBase == 0x400000;
            }
            if (_wcsicmp(entry.szModule, L"GrandMPNativeCounterExperimental.asi") != 0) continue;
            const auto loaded = readNativeCounterFile(entry.szExePath);
            if (!loaded || loaded->digest != expected->digest || loaded->bytes != expected->bytes ||
                entry.modBaseSize != expected->imageSize || currentBase > UINT32_MAX ||
                currentBase + expected->imageSize > UINT32_MAX) return 3;
            base = static_cast<std::uint32_t>(currentBase);
            ++matches;
        } while (Module32NextW(snapshot.value, &entry));
        if (GetLastError() != ERROR_NO_MORE_FILES || !mainVerified || matches != 1) {
            std::puts("INCONCLUSIVO: main base or experimental module absent/ambiguous"); return 3;
        }
        if (dryRun) {
            NativeCounterCall call{};
            NativeCounterPhysicalSnapshot facts{};
            facts.processId = pid;
            facts.executableBase = 0x400000;  // Independently checked in module enumeration above.
            facts.moduleBase = base;
            facts.loadedModuleFileDigest = expected->digest;  // Exact loaded file checked above.
            if (!read(process.value, base + *rva, &facts.first, sizeof(facts.first)) ||
                !read(process.value, 0x53E981, call.data(), call.size()) ||
                !read(process.value, base + *rva, &facts.second, sizeof(facts.second))) {
                std::puts("DRY_RUN REJECTED: incomplete read-only evidence"); return 3;
            }
            facts.callBytes = call;
            const auto result = dryRunNativeCounterAdmission(*image, *expected, facts);
            if (!result.plan) {
                std::printf("DRY_RUN REJECTED: preflight=%u writeEnabled=0\n",
                    static_cast<unsigned>(result.evidence));
                return result.evidence == NativeCounterPreflightResult::not_ready ? 4 : 3;
            }
            const auto& plan = *result.plan;
            std::printf("DRY_RUN ADMISSIBLE: callsite=%08X original=%08X wrapper=%08X writeEnabled=0\n",
                plan.callsite, plan.originalTarget, plan.wrapper);
            std::puts("Evidence only; no debug-stop ownership, preparation or installation authorization.");
            return 0;
        }
        NativeCounterReadyV1 first{}, second{};
        if (!read(process.value, base + *rva, &first, sizeof(first)) ||
            !read(process.value, base + *rva, &second, sizeof(second)) ||
            std::memcmp(&first, &second, sizeof(first)) || !stateName(second.state) ||
            second.magic != nativeCounterReadyMagic || second.interfaceVersion != nativeCounterReadyVersion ||
            second.size != sizeof(second) || second.residencyConfirmed > 1 || second.installed != 0 ||
            second.wrapperCapable != 0 || second.originalTarget != 0 || second.callsite != 0 || second.reserved != 0) {
            std::puts("INCONCLUSIVO: unstable/incompatible bootstrap descriptor"); return 3;
        }
        if (second.state == 2 && (second.residencyConfirmed != 1 || second.processId != pid ||
            second.moduleBase != base || second.moduleSize != expected->imageSize ||
            second.wrapper != base + *wrapperRva ||
            second.descriptorAddress != base + *rva || !second.threadId || second.shutdownLogical ||
            second.counterAddress < base || std::uint64_t(second.counterAddress) + 8 > std::uint64_t(base) + expected->imageSize ||
            second.counterAddress % 8)) return 3;
        std::uint64_t count = 0;
        if (second.state == 2 && (!read(process.value, second.counterAddress, &count, sizeof(count)) || count != 0)) return 3;
        std::wprintf(L"PID=%lu state=%ls PIN=%u bootstrapTID=%u count=%llu callbackCapable=0 installed=0\n",
            pid, stateName(second.state), second.residencyConfirmed, second.threadId,
            static_cast<unsigned long long>(count));
        std::wprintf(L"READY means residence only; no engine-thread or patch authorization.\n");
        return second.state == 2 ? 0 : 4;
    } catch (...) {
        std::puts("INCONCLUSIVO: bounded local evidence inspection failed"); return 3;
    }
}
