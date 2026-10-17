#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "grandmp/windows/asi_bootstrap.hpp"
#include "native_counter_gate.hpp"
#include "native_counter_ready.hpp"
#include "native_counter_preflight.hpp"
#include <atomic>
#include <cstring>
#include <cstdint>
#include <cstddef>
#include <optional>
#include <mutex>
#include <array>
#include <string_view>
#if defined(_M_IX86)
#include <intrin.h>
#endif

namespace {
HMODULE moduleHandle = nullptr;
unsigned char moduleAnchor = 0;
grandmp::windows::AsiBootstrapController bootstrap;
std::atomic<std::uint64_t> counter{0};
static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

std::atomic<bool> enabled{false};
std::mutex preparationMutex;
bool cancelled = false;
std::atomic<bool> asyncScheduled{false};
#ifdef GMP_NATIVE_COUNTER_HOST_TEST
HANDLE asyncTestGate = nullptr;
HANDLE asyncTestStarted = nullptr;
unsigned asyncTestFailure = 0;
#endif
void scheduleAsyncReady() noexcept;
// This function does exactly one operation. Gating belongs to the dispatcher.
void countCallback() noexcept { counter.fetch_add(1, std::memory_order_relaxed); }
}  // namespace

extern "C" {
grandmp::windows::NativeCounterReadyV1 GrandMPNativeCounterReady{
    grandmp::windows::nativeCounterReadyMagic,
    grandmp::windows::nativeCounterReadyVersion,
    sizeof(grandmp::windows::NativeCounterReadyV1)};
}

#if defined(_M_IX86)
namespace {
using OriginalUpdate = void(__cdecl*)() noexcept;
OriginalUpdate originalUpdate = nullptr;
bool wrapperCpuSupported() noexcept {
    int cpu[4]{};
    __cpuid(cpu, 1);
    constexpr int required = (1 << 24) | (1 << 25) | (1 << 26);  // FXSR, SSE, SSE2.
    return (cpu[3] & required) == required &&
        IsProcessorFeaturePresent(PF_XMMI_INSTRUCTIONS_AVAILABLE) &&
        IsProcessorFeaturePresent(PF_XMMI64_INSTRUCTIONS_AVAILABLE);
}
void __cdecl countAfterOriginal() noexcept {
    // No gameplay/IO/allocator/mutex. State is resident and originalUpdate is
    // immutable after READY. Logical disable only closes the counting gate.
    if (enabled.load(std::memory_order_acquire) &&
        std::atomic_ref(GrandMPNativeCounterReady.state).load(std::memory_order_acquire) ==
            static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::ready))
        countCallback();
}
}
// Demonstrated lab contract: void __cdecl(), no arguments. CALL original once, then
// preserve its integer registers, EFLAGS, x87/SIMD state and stack across the
// diagnostic helper, including any MSVC Debug runtime instrumentation.
extern "C" __declspec(naked) void __cdecl GrandMPNativeCounterWrapperHost() noexcept {
    __asm {
        pushfd
        cmp dword ptr [originalUpdate], 0
        je unprepared
        popfd
        call dword ptr [originalUpdate]
        pushfd
        pushad
        mov eax, esp
        sub esp, 528
        and esp, 0FFFFFFF0h
        mov dword ptr [esp + 512], eax
        fxsave [esp]
        call countAfterOriginal
        fxrstor [esp]
        mov esp, dword ptr [esp + 512]
        popad
        popfd
        ret
    unprepared:
        popfd
        ret
    }
}
#else
extern "C" void __cdecl GrandMPNativeCounterWrapperHost() noexcept {}
#endif

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) moduleHandle = module;
    return TRUE;
}

extern "C" void __cdecl InitializeASI() noexcept {
    (void)bootstrap.requestEntry();
    std::uint32_t dormant = 0;
    (void)std::atomic_ref(GrandMPNativeCounterReady.state).compare_exchange_strong(dormant,
        static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::requested),
        std::memory_order_release, std::memory_order_relaxed);
    wchar_t optIn[2]{};
    if (GetEnvironmentVariableW(L"GRANDMP_NATIVE_COUNTER_ASYNC_READY", optIn, 2) == 1 &&
        optIn[0] == L'1') scheduleAsyncReady();
}

extern "C" unsigned int __cdecl GrandMPNativeCounterInstallationStatus() noexcept {
    // Invariant, not an opt-in switch: GTA installation has no implementation.
    return static_cast<unsigned int>(grandmp::windows::NativeCounterGateResult::installation_unavailable);
}

extern "C" std::uint64_t __cdecl GrandMPNativeCounterRead() noexcept {
    return counter.load(std::memory_order_relaxed);
}

// Shared by explicit cooperative preparation and the Win32 residency worker.
// Neither DllMain nor InitializeASI calls this helper directly.
namespace {
int prepareResidence(int failBeforePin, bool activateCounter = true) noexcept {
    {
        std::lock_guard lock(preparationMutex);
        if (cancelled) return 0;
        if (bootstrap.beginDeferred() != grandmp::windows::AsiBootstrapResult::accepted) return 0;
        std::atomic_ref(GrandMPNativeCounterReady.state).store(
            static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::initializing),
            std::memory_order_release);
    }
    const auto failed = [] {
        std::lock_guard lock(preparationMutex);
        (void)bootstrap.publishResidence(false);
        if (!cancelled) std::atomic_ref(GrandMPNativeCounterReady.state).store(
            static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::failed),
            std::memory_order_release);
        return 0;
    };
    if (failBeforePin) {
        return failed();
    }
    HMODULE acquired = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                           reinterpret_cast<LPCWSTR>(&moduleAnchor), &acquired)) {
        return failed();
    }
    if (acquired != moduleHandle) {
        FreeLibrary(acquired);
        return failed();
    }
    HMODULE pinned = nullptr;
    const BOOL success = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<LPCWSTR>(&moduleAnchor), &pinned);
    FreeLibrary(acquired);
    if (!success || pinned != moduleHandle) {
        return failed();
    }
    // PIN cannot be undone in this process. No teardown modifies executable
    // bytes; logical disable never attempts unload. Host owns source quiescence.
    std::lock_guard lock(preparationMutex);
    if (bootstrap.publishResidence(true) != grandmp::windows::AsiBootstrapResult::completed) return 0;
    GrandMPNativeCounterReady.residencyConfirmed = 1;
    if (cancelled) {
        (void)bootstrap.disableLogically();
        return 0;
    }
    enabled.store(activateCounter, std::memory_order_release);
    return 1;
}

#if defined(_M_IX86)
// Worker only. No export invokes this and no game function is executed here.
std::optional<grandmp::windows::NativeCounterReadyV1> prepareHoodlumWorker(
    const grandmp::windows::NativeCounterReadyV1& initial) noexcept {
    using namespace grandmp::windows;
    wchar_t optIn[2]{};
    if (GetEnvironmentVariableW(L"GRANDMP_NATIVE_COUNTER_PREPARE_HOODLUM", optIn, 2) != 1 ||
        optIn[0] != L'1' || !wrapperCpuSupported()) return {};
    const auto codePage = [](HMODULE owner, std::uint32_t address, std::size_t size) {
        MEMORY_BASIC_INFORMATION page{};
        return VirtualQuery(reinterpret_cast<LPCVOID>(address), &page, sizeof(page)) == sizeof(page) &&
            page.AllocationBase == owner && page.State == MEM_COMMIT && page.Type == MEM_IMAGE &&
            page.Protect == PAGE_EXECUTE_READ &&
            std::uint64_t(address) + size <= reinterpret_cast<std::uintptr_t>(page.BaseAddress) + page.RegionSize;
    };
    const auto wrapperAddress = reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterWrapperHost);
    if (initial.wrapper != wrapperAddress || !codePage(moduleHandle, wrapperAddress, 1)) return {};
    try {
        const HMODULE executable = GetModuleHandleW(nullptr);
        if (reinterpret_cast<std::uintptr_t>(executable) != 0x400000) return {};
        wchar_t executablePath[32768]{}, modulePath[32768]{};
        const auto executableLength = GetModuleFileNameW(executable, executablePath, 32768);
        const auto moduleLength = GetModuleFileNameW(moduleHandle, modulePath, 32768);
        if (!executableLength || executableLength >= 32768 || !moduleLength || moduleLength >= 32768) return {};
        const auto image = readNativeCounterFile(executablePath);
        const auto module = readNativeCounterFile(modulePath);
        if (!image || !module || recognizeGameVersion(image->inspection, image->digest) !=
            GameVersion::gta_sa_10us_hoodlum) return {};
        const auto read = [](std::uint32_t address, void* data, std::size_t size) {
            SIZE_T got = 0;
            return ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address),
                data, size, &got) && got == size;
        };
        // Match the mapped PE32 header to the admitted file before fixed-address reads.
        IMAGE_DOS_HEADER dos{};
        IMAGE_NT_HEADERS32 nt{};
        if (!read(0x400000, &dos, sizeof(dos)) || dos.e_magic != IMAGE_DOS_SIGNATURE ||
            dos.e_lfanew <= 0 || dos.e_lfanew > 4096 - static_cast<LONG>(sizeof(nt)) ||
            !read(0x400000 + dos.e_lfanew, &nt, sizeof(nt)) || nt.Signature != IMAGE_NT_SIGNATURE ||
            nt.FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
            nt.OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC ||
            nt.OptionalHeader.ImageBase != image->imageBase || nt.OptionalHeader.SizeOfImage != image->imageSize)
            return {};
        if (!codePage(executable, 0x53E981, 5) || !codePage(executable, 0x53BEE0, 1)) return {};
        NativeCounterCall call{};
        if (!read(0x53E981, call.data(), call.size())) return {};
        NativeCounterPhysicalSnapshot facts{};
        facts.processId = GetCurrentProcessId();
        facts.executableBase = 0x400000;
        facts.moduleBase = reinterpret_cast<std::uint32_t>(moduleHandle);
        facts.loadedModuleFileDigest = module->digest;
        facts.first = facts.second = initial;
        facts.callBytes = call;
        const auto candidate = prepareNativeCounterHoodlumDescriptor(*image, *module, facts);
        NativeCounterCall confirmed{};
        if (!candidate || !read(0x53E981, confirmed.data(), confirmed.size()) || confirmed != call ||
            !codePage(executable, 0x53E981, 5) || !codePage(executable, 0x53BEE0, 1) ||
            !codePage(moduleHandle, wrapperAddress, 1)) return {};
        return candidate;
    } catch (...) {
        return {};  // File/PE/allocation failure cannot publish capability.
    }
}
#endif

// The scheduler owns a counted module reference before the OS can dispatch
// this start routine. Thread DLL notifications are serialized by Windows;
// the worker never runs inside InitializeASI or DllMain. Hoodlum binding is
// separately opt-in and completes before the single READY publication.
DWORD WINAPI asyncReadyWorker(void* reference) noexcept {
    const HMODULE owned = static_cast<HMODULE>(reference);
    bool permitted = true;
    int failBeforePin = 0;
#ifdef GMP_NATIVE_COUNTER_HOST_TEST
    if (asyncTestStarted) SetEvent(asyncTestStarted);
    if (asyncTestGate) {
        permitted = WaitForSingleObject(asyncTestGate, 5000) == WAIT_OBJECT_0;
        CloseHandle(asyncTestGate);
        asyncTestGate = nullptr;
    }
    if (asyncTestStarted) {
        CloseHandle(asyncTestStarted);
        asyncTestStarted = nullptr;
    }
    failBeforePin = asyncTestFailure == 2 ? 1 : 0;
#endif
#if defined(_M_IX86)
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleHandle);
    const IMAGE_NT_HEADERS32* nt = nullptr;
    if (dos->e_magic == IMAGE_DOS_SIGNATURE && dos->e_lfanew > 0 &&
        dos->e_lfanew <= 4096 - static_cast<LONG>(sizeof(IMAGE_NT_HEADERS32))) {
        nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
            reinterpret_cast<const unsigned char*>(moduleHandle) + dos->e_lfanew);
    }
    permitted = permitted && nt && nt->Signature == IMAGE_NT_SIGNATURE &&
        nt->FileHeader.Machine == IMAGE_FILE_MACHINE_I386 &&
        nt->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    if (prepareResidence(permitted ? failBeforePin : 1, false)) {
        grandmp::windows::NativeCounterReadyV1 initial{};
        {
            std::lock_guard lock(preparationMutex);
            if (!cancelled) {
                initial = GrandMPNativeCounterReady;
                initial.processId = GetCurrentProcessId();
                initial.moduleBase = reinterpret_cast<std::uint32_t>(moduleHandle);
                initial.moduleSize = nt->OptionalHeader.SizeOfImage;
                initial.wrapper = reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterWrapperHost);
                initial.counterAddress = reinterpret_cast<std::uint32_t>(&counter);
                initial.descriptorAddress = reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterReady);
                initial.threadId = GetCurrentThreadId();
            }
        }
        // No loader/file/provider operations while holding preparationMutex.
        const auto prepared = initial.threadId ? prepareHoodlumWorker(initial) : std::nullopt;
        std::lock_guard lock(preparationMutex);
        if (!cancelled) {
            auto publication = prepared ? *prepared : initial;
            publication.state = static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::initializing);
            if (prepared) originalUpdate = reinterpret_cast<OriginalUpdate>(prepared->originalTarget);
            enabled.store(prepared.has_value(), std::memory_order_release);
            // Never overwrite state non-atomically while observers poll it.
            constexpr auto stateOffset = offsetof(grandmp::windows::NativeCounterReadyV1, state);
            constexpr auto afterState = offsetof(grandmp::windows::NativeCounterReadyV1, residencyConfirmed);
            static_assert(afterState == stateOffset + sizeof(publication.state),
                "Partial publication must skip exactly the state field");
            std::memcpy(&GrandMPNativeCounterReady, &publication, stateOffset);
            std::memcpy(reinterpret_cast<unsigned char*>(&GrandMPNativeCounterReady) + afterState,
                reinterpret_cast<const unsigned char*>(&publication) + afterState, sizeof(publication) - afterState);
            // installed stays zero. Binding does not dispatch the wrapper or install a CALL.
            std::atomic_ref(GrandMPNativeCounterReady.state).store(
                static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::ready),
                std::memory_order_release);
        }
    }
#else
    (void)permitted;
    (void)failBeforePin;
    (void)prepareResidence(1);  // READY v1 represents x86 addresses only.
#endif
    // Atomic release-and-exit avoids returning into possibly unloaded code
    // when initialization failed or shutdown won before PIN.
    FreeLibraryAndExitThread(owned, 0);
}

void scheduleAsyncReady() noexcept {
    bool expected = false;
    if (!asyncScheduled.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) return;
    // InitializeASI never waits for a worker-owned mutex. The worker makes the
    // definitive cancellation decision; racing shutdown cannot revive READY.
    if (std::atomic_ref(GrandMPNativeCounterReady.state).load(std::memory_order_acquire) !=
        static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::requested)) return;
    HMODULE owned = nullptr;
    bool acquired = false;
#ifdef GMP_NATIVE_COUNTER_HOST_TEST
    if (asyncTestFailure != 3)
#endif
        acquired = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(&moduleAnchor), &owned) != FALSE;
    if (acquired && owned != moduleHandle) {
        FreeLibrary(owned);
        acquired = false;
    }
    HANDLE thread = nullptr;
    if (acquired) {
#ifdef GMP_NATIVE_COUNTER_HOST_TEST
        if (asyncTestFailure != 1)
#endif
            thread = CreateThread(nullptr, 0, asyncReadyWorker, owned, 0, nullptr);
    }
    if (thread) {
        CloseHandle(thread);
        return;
    }
    if (acquired) FreeLibrary(owned);  // Caller still owns its LoadLibrary ref.
#ifdef GMP_NATIVE_COUNTER_HOST_TEST
    if (asyncTestGate) { CloseHandle(asyncTestGate); asyncTestGate = nullptr; }
    if (asyncTestStarted) { CloseHandle(asyncTestStarted); asyncTestStarted = nullptr; }
#endif
    if (bootstrap.beginDeferred() == grandmp::windows::AsiBootstrapResult::accepted) {
        (void)bootstrap.publishResidence(false);
        std::uint32_t requested = static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::requested);
        (void)std::atomic_ref(GrandMPNativeCounterReady.state).compare_exchange_strong(requested,
            static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::failed),
            std::memory_order_release, std::memory_order_relaxed);
    }
}
}  // namespace

#ifdef GMP_NATIVE_COUNTER_HOST_TEST
extern "C" int __cdecl GrandMPNativeCounterConfigureAsyncTest(
    HANDLE gate, HANDLE started, unsigned failure) noexcept {
    std::lock_guard lock(preparationMutex);
    if (asyncScheduled.load() || asyncTestGate || asyncTestStarted ||
        std::atomic_ref(GrandMPNativeCounterReady.state).load(std::memory_order_acquire) != 0 ||
        failure > 3) return 0;
    HANDLE gateCopy = nullptr, startedCopy = nullptr;
    if (gate && !DuplicateHandle(GetCurrentProcess(), gate, GetCurrentProcess(), &gateCopy,
        SYNCHRONIZE, FALSE, 0)) return 0;
    if (started && !DuplicateHandle(GetCurrentProcess(), started, GetCurrentProcess(), &startedCopy,
        EVENT_MODIFY_STATE, FALSE, 0)) {
        if (gateCopy) CloseHandle(gateCopy);
        return 0;
    }
    asyncTestGate = gateCopy;
    asyncTestStarted = startedCopy;
    asyncTestFailure = failure;
    return 1;
}
extern "C" int __cdecl GrandMPNativeCounterPrepareHost(int failBeforePin) noexcept {
    return prepareResidence(failBeforePin);
}
extern "C" void __cdecl GrandMPNativeCounterDispatchHost() noexcept {
    if (enabled.load(std::memory_order_acquire)) countCallback();
}
#endif

extern "C" void __cdecl GrandMPNativeCounterShutdown() noexcept {
    std::lock_guard lock(preparationMutex);
    cancelled = true;
    enabled.store(false, std::memory_order_release);
    GrandMPNativeCounterReady.shutdownLogical = 1;
    std::atomic_ref(GrandMPNativeCounterReady.state).store(
        static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::disabled),
        std::memory_order_release);
    (void)bootstrap.disableLogically();
}
#ifdef GMP_NATIVE_COUNTER_HOST_TEST
extern "C" void __cdecl GrandMPNativeCounterDisableHost() noexcept {
    GrandMPNativeCounterShutdown();
}
#endif

namespace {
int prepareTransaction(
    std::uint32_t callsite, std::uint32_t target) noexcept {
#if defined(_M_IX86)
    std::lock_guard lock(preparationMutex);
    if (cancelled || !enabled.load(std::memory_order_acquire) || originalUpdate ||
        !callsite || !target || target == reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterWrapperHost))
        return 0;
    if (!wrapperCpuSupported()) return 0;
    // The cooperative host's known code was admitted by the caller. The
    // arbitrary-address exported entry exists only in the separate lab DLL.
    const auto* instruction = reinterpret_cast<const unsigned char*>(callsite);
    if (instruction[0] != 0xE8) return 0;
    std::int32_t displacement = 0;
    std::memcpy(&displacement, instruction + 1, sizeof(displacement));
    if (static_cast<std::uint32_t>(callsite + 5 + displacement) != target) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(moduleHandle);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        reinterpret_cast<const unsigned char*>(moduleHandle) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return 0;
    originalUpdate = reinterpret_cast<OriginalUpdate>(target);
    auto& ready = GrandMPNativeCounterReady;
    ready.processId = GetCurrentProcessId();
    ready.moduleBase = reinterpret_cast<std::uint32_t>(moduleHandle);
    ready.moduleSize = nt->OptionalHeader.SizeOfImage;
    ready.residencyConfirmed = 1;
    ready.wrapper = reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterWrapperHost);
    ready.originalTarget = target;
    ready.callsite = callsite;
    ready.counterAddress = reinterpret_cast<std::uint32_t>(&counter);
    ready.descriptorAddress = reinterpret_cast<std::uint32_t>(&ready);
    ready.wrapperCapable = 1;
    ready.threadId = GetCurrentThreadId();
    std::memcpy(&ready.originalBytesLow, instruction, 4);
    ready.originalBytesHigh = instruction[4];
    std::atomic_ref(ready.state).store(
        static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::ready),
        std::memory_order_release);
    return 1;
#else
    (void)callsite;
    (void)target;
    return 0;
#endif
}
}  // namespace
#ifdef GMP_NATIVE_COUNTER_HOST_TEST
extern "C" int __cdecl GrandMPNativeCounterPrepareTransaction(
    std::uint32_t callsite, std::uint32_t target) noexcept {
    return prepareTransaction(callsite, target);
}
#endif

extern "C" int __cdecl GrandMPNativeCounterPrepareDeferred() noexcept {
#if defined(_M_IX86)
    // Admission is deliberately restricted to the existing cooperative host.
    // The export accepts no arbitrary address or context claim. It is never
    // invoked by UAL and provides no automatic preparation in gta_sa.exe.
    wchar_t path[32768]{};
    const HMODULE host = GetModuleHandleW(nullptr);
    const DWORD length = GetModuleFileNameW(host, path, static_cast<DWORD>(std::size(path)));
    if (!length || length >= std::size(path)) return 0;
    const std::wstring_view full{path, length};
    const auto separator = full.find_last_of(L"\\/");
    const auto name = full.substr(separator == std::wstring_view::npos ? 0 : separator + 1);
    if (name != L"grandmp_native_counter_host.exe") return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(host);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(
        reinterpret_cast<const unsigned char*>(host) + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 ||
        nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return 0;
    const auto callsite = reinterpret_cast<std::uint32_t>(GetProcAddress(host,
        "GrandMPNativeCounterHostCallsite"));
    const auto target = reinterpret_cast<std::uint32_t>(GetProcAddress(host,
        "GrandMPNativeCounterHostOriginal"));
    const auto base = reinterpret_cast<std::uint32_t>(host);
    const auto size = nt->OptionalHeader.SizeOfImage;
    if (size < 5 || !callsite || !target || callsite < base || target < base ||
        callsite - base > size - 5 || target - base >= size) return 0;
    MEMORY_BASIC_INFORMATION page{};
    if (!VirtualQuery(reinterpret_cast<void*>(callsite), &page, sizeof(page)) ||
        page.AllocationBase != host || page.State != MEM_COMMIT || page.Type != MEM_IMAGE ||
        page.Protect != PAGE_EXECUTE_READ) return 0;
    if (callsite + 5 > reinterpret_cast<std::uint32_t>(page.BaseAddress) + page.RegionSize)
        return 0;
    if (!VirtualQuery(reinterpret_cast<void*>(target), &page, sizeof(page)) ||
        page.AllocationBase != host || page.State != MEM_COMMIT || page.Type != MEM_IMAGE ||
        page.Protect != PAGE_EXECUTE_READ) return 0;
    // Reject incompatible bytes before any irreversible PIN operation. The
    // host is cooperative: it does not modify these bytes while preparing.
    const auto* bytes = reinterpret_cast<const unsigned char*>(callsite);
    std::int32_t delta{};
    std::memcpy(&delta, bytes + 1, 4);
    if (bytes[0] != 0xE8 || static_cast<std::uint32_t>(callsite + 5 + delta) != target) {
        std::lock_guard lock(preparationMutex);
        if (cancelled) return 0;
        if (bootstrap.beginDeferred() == grandmp::windows::AsiBootstrapResult::accepted) {
            (void)bootstrap.publishResidence(false);
            std::atomic_ref(GrandMPNativeCounterReady.state).store(
                static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::failed),
                std::memory_order_release);
        }
        return 0;
    }
    if (!prepareResidence(0)) return 0;
    if (prepareTransaction(callsite, target)) return 1;
    // Residence may be irreversible, but readiness is never published after a
    // failed bind. This is not hot-unload support.
    std::lock_guard lock(preparationMutex);
    enabled.store(false, std::memory_order_release);
    if (!cancelled)
        std::atomic_ref(GrandMPNativeCounterReady.state).store(
            static_cast<std::uint32_t>(grandmp::windows::NativeCounterReadyState::failed),
            std::memory_order_release);
    return 0;
#else
    return 0;
#endif
}
