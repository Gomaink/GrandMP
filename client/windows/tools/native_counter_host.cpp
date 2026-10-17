#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "native_counter_gate.hpp"
#include "native_counter_ready.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <semaphore>
#include <string_view>
#include <thread>
#include <vector>

#if defined(_M_IX86)
namespace {
volatile LONG originalCallCount = 0;
}
// Code exists in this EXE's normal RX image; no RWX buffer or generated code.
extern "C" __declspec(naked) void __cdecl GrandMPNativeCounterHostOriginal() noexcept {
    __asm {
        lock inc dword ptr [originalCallCount]
        mov eax, 11223344h
        mov ecx, 55667788h
        mov edx, 12345678h
        cmp eax, eax
        ret
    }
}
extern "C" __declspec(naked) void __cdecl GrandMPNativeCounterHostCallsite() noexcept {
    __asm {
        call GrandMPNativeCounterHostOriginal
        ret
    }
}
extern "C" __declspec(naked) void __cdecl GrandMPNativeCounterHostForeignOriginal() noexcept {
    __asm { jmp GrandMPNativeCounterHostOriginal }
}
#endif

namespace {
using Entry = void(__cdecl*)() noexcept;
using Prepare = int(__cdecl*)(int) noexcept;
using Read = std::uint64_t(__cdecl*)() noexcept;
using Status = unsigned int(__cdecl*)() noexcept;
using PrepareTransaction = int(__cdecl*)(std::uint32_t, std::uint32_t) noexcept;

bool check(bool value, const char* description) {
    if (!value) std::fprintf(stderr, "FAIL: %s\n", description);
    return value;
}

// Real host routine with a known signature. The source forwards to it once.
__declspec(noinline) void __cdecl originalHostUpdate(std::atomic<std::uint64_t>& calls) noexcept {
    calls.fetch_add(1, std::memory_order_relaxed);
}

// Specific cooperative source, owned by this disposable process. This is a
// pointer publication test, not a CALL patcher or an emulation of GTA threads.
class CounterSource final {
public:
    bool install(Entry callback, bool failBeforePublish = false) {
        std::lock_guard lock(mutex_);
        if (!callback || callback_ || stopped_ || failBeforePublish) return false;
        callback_ = callback;
        return true;
    }
    void update() {
        // Known host routine called exactly once, regardless of callback state.
        originalHostUpdate(originals);
        Entry target = nullptr;
        {
            std::lock_guard lock(mutex_);
            if (!stopped_ && callback_ && !inDispatch_) {
                ++active_;
                target = callback_;
            }
        }
        if (!target) return;
        inDispatch_ = true;
        target();
        if (recursiveOnce.exchange(false)) update();
        inDispatch_ = false;
        {
            std::lock_guard lock(mutex_);
            --active_;
        }
        drained_.notify_all();
    }
    bool shutdown() {
        // A reentrant shutdown cannot wait for its own active invocation.
        if (inDispatch_) return false;
        std::unique_lock lock(mutex_);
        stopped_ = true;
        callback_ = nullptr;
        drained_.wait(lock, [&] { return active_ == 0; });
        return true;
    }
    bool stopped() { std::lock_guard lock(mutex_); return stopped_; }
    std::atomic<std::uint64_t> originals{0};
    std::atomic<bool> recursiveOnce{false};
private:
    std::mutex mutex_;
    std::condition_variable drained_;
    Entry callback_ = nullptr;
    unsigned active_ = 0;
    bool stopped_ = false;
    static inline thread_local bool inDispatch_ = false;
};

std::binary_semaphore callbackEntered{0};
std::binary_semaphore releaseCallback{0};
Entry heldTarget = nullptr;
void __cdecl heldCallback() noexcept {
    callbackEntered.release();
    releaseCallback.acquire();
    heldTarget();
}

CounterSource* reentrantSource = nullptr;
Entry reentrantTarget = nullptr;
bool nestedShutdownRefused = false;
void __cdecl reentrantCallback() noexcept {
    reentrantTarget();
    nestedShutdownRefused = !reentrantSource->shutdown();
}

bool gateCase() {
    using namespace grandmp::windows;
    const ExecutableInspection known{PeStatus::pe32_i386, 0, 14'383'616};
    constexpr std::array<std::byte, 32> digest{
        std::byte{0xA5},std::byte{0x59},std::byte{0xAA},std::byte{0x77},
        std::byte{0x2F},std::byte{0xD1},std::byte{0x36},std::byte{0x37},
        std::byte{0x91},std::byte{0x55},std::byte{0xEF},std::byte{0xA7},
        std::byte{0x1F},std::byte{0x00},std::byte{0xC4},std::byte{0x7A},
        std::byte{0xAD},std::byte{0x34},std::byte{0xBB},std::byte{0xFE},
        std::byte{0xAE},std::byte{0x61},std::byte{0x96},std::byte{0xB0},
        std::byte{0xFE},std::byte{0x10},std::byte{0x47},std::byte{0xD0},
        std::byte{0x64},std::byte{0x5C},std::byte{0xBD},std::byte{0x26}};
    bool ok = check(inspectNativeCounterGate(false, known, digest, nativeCounterOriginalCall) ==
        NativeCounterGateResult::disabled, "disabled default");
    ok &= check(inspectNativeCounterGate(true, known, digest, nativeCounterOriginalCall) ==
        NativeCounterGateResult::installation_unavailable, "valid evidence still NO-GO");
    auto badHash = digest;
    badHash[0] ^= std::byte{1};
    ok &= check(inspectNativeCounterGate(true, known, badHash, nativeCounterOriginalCall) ==
        NativeCounterGateResult::unknown_executable, "unknown fingerprint");
    auto wrongArchitecture = known;
    wrongArchitecture.status = PeStatus::unsupported_architecture;
    ok &= check(inspectNativeCounterGate(true, wrongArchitecture, digest, nativeCounterOriginalCall) ==
        NativeCounterGateResult::unknown_executable, "wrong architecture");
    for (std::size_t i = 0; i < nativeCounterOriginalCall.size(); ++i) {
        auto altered = nativeCounterOriginalCall;
        altered[i] ^= std::byte{1};
        ok &= check(inspectNativeCounterGate(true, known, digest, altered) ==
            NativeCounterGateResult::incompatible_bytes, "each altered byte rejected");
    }
    auto thirdParty = nativeCounterOriginalCall;
    thirdParty[0] = std::byte{0xE9};
    ok &= check(inspectNativeCounterGate(true, known, digest, thirdParty) ==
        NativeCounterGateResult::incompatible_bytes, "preexisting patch");
    ok &= check(inspectNativeCounterGate(true, known, digest,
        std::span(nativeCounterOriginalCall).first(4)) == NativeCounterGateResult::incompatible_bytes,
        "partial read");
    return ok;
}

// Already-running disposable fixture. The test supervisor owns its lifetime;
// the attaching debugger neither starts it nor calls exports in it remotely.
bool attachTarget(HMODULE module, Entry entry, Prepare prepare, Read read, bool invalid, bool dispatch = false) {
#if defined(_M_IX86)
    (void)std::setvbuf(stdout, nullptr, _IONBF, 0);
    const auto transaction = reinterpret_cast<PrepareTransaction>(
        GetProcAddress(module, "GrandMPNativeCounterPrepareTransaction"));
    auto* ready = reinterpret_cast<grandmp::windows::NativeCounterReadyV1*>(
        GetProcAddress(module, "GrandMPNativeCounterReady"));
    if (!transaction || !ready) return false;
    entry();
    if (prepare(0) != 1 || transaction(reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterHostCallsite),
        reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterHostOriginal)) != 1) return false;
    if (invalid) ready->state = 3; // Own lab data only, before the debugger attaches.
    const auto before = *ready;
    std::array<unsigned char, 5> original{};
    std::memcpy(original.data(), reinterpret_cast<const void*>(&GrandMPNativeCounterHostCallsite), original.size());
    std::atomic<bool> done{false};
    wchar_t inventoryModeBuffer[64]{};
    GetEnvironmentVariableW(L"GRANDMP_LAB_INVENTORY_MODE", inventoryModeBuffer, 64);
    const std::wstring_view inventoryMode(inventoryModeBuffer);
    const bool churn = dispatch && !inventoryMode.empty();
    HMODULE inventoryModule = nullptr;
    if (churn && (inventoryMode == L"modules_remove" || inventoryMode == L"modules_reload")) {
        inventoryModule = LoadLibraryW(L"GrandMPNativeInventoryProbe.dll");
        if (!check(inventoryModule != nullptr, "pre-attach inventory probe load")) return false;
    }
    std::atomic<bool> retire{false};
    std::thread retiring;
    if (churn && (inventoryMode == L"threads_exit" || inventoryMode == L"threads_churn")) {
        retiring = std::thread([&] { while (!retire.load() && !done.load()) Sleep(5); });
    }
    std::atomic<unsigned> started{0};
    std::atomic<std::uint64_t> calls{0};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 3; ++i) workers.emplace_back([&, i] {
        ++started;
        while (!done.load()) {
            if (dispatch) { GrandMPNativeCounterHostCallsite(); ++calls; }
            if (i == 0) std::puts("attach_tick=1");
            Sleep(100);
        }
    });
    while (started.load() != 3) Sleep(1);
    std::atomic<bool> inventoryComplete{false};
    std::thread inventoryWorker;
    if (churn) inventoryWorker = std::thread([&] {
        // Wait for a real installed descriptor, rather than racing attach.
        while (!done.load() && std::atomic_ref(ready->installed).load() != 1) Sleep(1);
        if (done.load()) return;
        if (retiring.joinable()) { retire = true; retiring.join(); }
        std::thread transient;
        if (inventoryMode == L"threads_create" || inventoryMode == L"threads_churn")
            transient = std::thread([&] { while (!done.load()) Sleep(5); });
        bool changed = true;
        if (inventoryMode == L"modules_add") {
            inventoryModule = LoadLibraryW(L"GrandMPNativeInventoryProbe.dll");
            changed = inventoryModule != nullptr;
        } else if (inventoryMode == L"modules_remove" || inventoryMode == L"modules_reload") {
            changed = FreeLibrary(inventoryModule) != FALSE;
            inventoryModule = nullptr;
            if (inventoryMode == L"modules_reload") {
                inventoryModule = LoadLibraryW(L"GrandMPNativeInventoryProbe.dll");
                changed &= inventoryModule != nullptr;
            }
        }
        std::printf("inventory_fixture mode=%ls changed=%u installed=%u\n",
            inventoryMode.data(), changed ? 1U : 0U, ready->installed);
        inventoryComplete = changed;
        // The newly created worker remains live through the second stop.
        if (transient.joinable()) transient.join();
        while (!done.load()) Sleep(5);
    });
    std::puts("attach_target_ready=1");
    (void)std::getchar(); // Explicit shutdown only from the independent owner.
    done = true;
    if (inventoryWorker.joinable()) inventoryWorker.join();
    if (retiring.joinable()) retiring.join();
    if (inventoryModule) FreeLibrary(inventoryModule);
    for (auto& worker : workers) worker.join();
    const bool ok = (!churn || inventoryComplete.load()) && !IsDebuggerPresent() && ready->installed == 0 &&
        (dispatch ? read() > 0 && static_cast<std::uint64_t>(originalCallCount) == calls.load() : read() == 0) &&
        std::memcmp(ready, &before, sizeof(before)) == 0 &&
        std::memcmp(original.data(), reinterpret_cast<const void*>(&GrandMPNativeCounterHostCallsite), original.size()) == 0;
    std::printf("attach_target_intact=%u installed=%u count=%llu debugger=%u\n",
                ok ? 1U : 0U, ready->installed, read(), IsDebuggerPresent() ? 1U : 0U);
    return ok;
#else
    return false;
#endif
}

bool debugTarget(HMODULE module, Entry entry, Prepare prepare, Entry disable, Read read, bool restore = false) {
#if defined(_M_IX86)
    if (!check(IsDebuggerPresent() != FALSE, "debug_target requires external debugger")) return false;
    const auto transaction = reinterpret_cast<PrepareTransaction>(
        GetProcAddress(module, "GrandMPNativeCounterPrepareTransaction"));
    auto* ready = reinterpret_cast<grandmp::windows::NativeCounterReadyV1*>(
        GetProcAddress(module, "GrandMPNativeCounterReady"));
    if (!check(transaction && ready, "READY v1 exports")) return false;
    entry();
    if (!check(prepare(0) == 1, "deferred explicit PIN before READY")) return false;
    if (!check(transaction(reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterHostCallsite),
        reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterHostOriginal)) == 1,
        "bind exact RX host CALL and original")) return false;
    if (!check(transaction(reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterHostCallsite),
        reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterHostOriginal)) == 0,
        "duplicate preparation rejected")) return false;
    entry();
    if (!check(ready->state == 2, "duplicate InitializeASI cannot regress READY")) return false;

    HANDLE run = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (!check(run != nullptr, "workers start barrier")) return false;
    std::atomic<bool> waitFailed{false};
    std::vector<std::thread> workers;
    for (unsigned i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            if (WaitForSingleObject(run, 15000) != WAIT_OBJECT_0) {
                waitFailed = true;
                return;
            }
            for (unsigned call = 0; call < 2000; ++call) GrandMPNativeCounterHostCallsite();
        });
    }
    // The real OUTPUT_DEBUG_STRING_EVENT freezes every target thread until the
    // external owner completes its transaction and ContinueDebugEvent.
    OutputDebugStringA(grandmp::windows::nativeCounterReadyMarker);
    const bool installed = ready->installed == 1;
    bool ok = check(ready->installed <= 1, "external commit marker valid");

    DWORD beforeEsp = 0, afterEsp = 0, eaxValue = 0, ecxValue = 0, edxValue = 0, flags = 0;
    DWORD beforeEbx = 0, afterEbx = 0, beforeEsi = 0, afterEsi = 0;
    DWORD beforeEdi = 0, afterEdi = 0, beforeEbp = 0, afterEbp = 0;
    const std::uint32_t xmmPattern[4]{0x11223344, 0x55667788, 0x99AABBCC, 0xDDEEFF00};
    std::uint32_t xmmAfter[4]{};
    double x87After = 0;
    __asm {
        movdqu xmm0, xmmPattern
        fld1
        mov beforeEsp, esp
        mov beforeEbx, ebx
        mov beforeEsi, esi
        mov beforeEdi, edi
        mov beforeEbp, ebp
        call GrandMPNativeCounterHostCallsite
        mov eaxValue, eax
        mov ecxValue, ecx
        mov edxValue, edx
        pushfd
        pop flags
        mov afterEsp, esp
        mov afterEbx, ebx
        mov afterEsi, esi
        mov afterEdi, edi
        mov afterEbp, ebp
        movdqu xmmAfter, xmm0
        fstp qword ptr [x87After]
    }
    ok &= check(beforeEsp == afterEsp && eaxValue == 0x11223344 &&
        ecxValue == 0x55667788 && edxValue == 0x12345678 && (flags & 0x8D5) == 0x44,
        "wrapper preserves original return registers arithmetic flags and ESP");
    ok &= check(beforeEbx == afterEbx && beforeEsi == afterEsi && beforeEdi == afterEdi &&
        beforeEbp == afterEbp, "wrapper preserves all callee-saved integer registers");
    ok &= check(std::equal(std::begin(xmmPattern), std::end(xmmPattern), std::begin(xmmAfter)) &&
        x87After == 1.0, "counter helper leaves XMM0 and x87 sentinel unchanged");
    ok &= check(SetEvent(run) != FALSE, "release workers after transaction");
    for (auto& worker : workers) worker.join();
    CloseHandle(run);
    ok &= check(!waitFailed, "all workers dispatched after stop");
    auto beforeShutdown = read();
    ok &= check(originalCallCount == 8001 && beforeShutdown == (installed ? 8001 : 0),
        "one original and optional count per concurrent CALL");
    unsigned extraCalls = 0;
    if (restore) {
        ok &= check(installed, "restore phase requires committed installation");
        // All workers have joined, but their exit events must still be accounted
        // for by the debugger. This is a new real all-stop, never a local flag.
        OutputDebugStringA("GRANDMP_NATIVE_COUNTER_RESTORE_V1");
        const bool remainsInstalled = ready->installed == 1;
        ok &= check(ready->installed <= 1 && ready->state == 2 && ready->residencyConfirmed == 1 &&
            ready->wrapperCapable == 1, "restore retains READY/PIN and prepared wrapper");
        for (unsigned call = 0; call < 100; ++call) GrandMPNativeCounterHostCallsite();
        extraCalls = 100;
        ok &= check(originalCallCount == 8101 && read() == beforeShutdown + (remainsInstalled ? 100 : 0),
            "original exactly once after restore or rollback; restored CALL stops new increments");
        beforeShutdown = read();
        std::printf("restore_host installed=%u count=%llu original=%ld resident=1\n",
            ready->installed, static_cast<unsigned long long>(beforeShutdown), originalCallCount);
    }
    disable();
    entry();
    ok &= check(ready->state == 3, "duplicate InitializeASI cannot regress disabled");
    for (unsigned call = 0; call < 100; ++call) GrandMPNativeCounterHostCallsite();
    ok &= check(originalCallCount == 8101 + extraCalls && read() == beforeShutdown &&
        ready->shutdownLogical == 1 && ready->state == 3,
        "logical shutdown suppresses callback but always forwards original");
    wchar_t path[32768]{};
    const auto length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    ok &= check(length != 0 && length < std::size(path), "lab image path");
    ok &= check(FreeLibrary(module) != FALSE && GetModuleHandleW(path) != nullptr,
        "PIN retains wrapper and READY after releasing external load reference");
    for (unsigned call = 0; call < 10; ++call) GrandMPNativeCounterHostCallsite();
    ok &= check(originalCallCount == 8111 + extraCalls && read() == beforeShutdown,
        "resident logically disabled code callable after reference release");
    std::printf("native_counter debug_target %s installed=%u original=%ld count=%llu ABI=ok resident=1 shutdown=1\n",
        ok ? "PASS" : "FAIL", installed ? 1u : 0u, originalCallCount,
        static_cast<unsigned long long>(beforeShutdown));
    return ok;
#else
    (void)module; (void)entry; (void)prepare; (void)disable; (void)read; (void)restore;
    return false;
#endif
}
}  // namespace

bool asiCase(HMODULE module, const std::wstring_view scenario, Entry entry, Read read) {
#if defined(_M_IX86)
    using Deferred = int(__cdecl*)() noexcept;
    const auto prepare = reinterpret_cast<Deferred>(GetProcAddress(module, "GrandMPNativeCounterPrepareDeferred"));
    const auto shutdown = reinterpret_cast<Entry>(GetProcAddress(module, "GrandMPNativeCounterShutdown"));
    const auto wrapper = reinterpret_cast<Entry>(GetProcAddress(module, "GrandMPNativeCounterWrapperHost"));
    auto* ready = reinterpret_cast<grandmp::windows::NativeCounterReadyV1*>(
        GetProcAddress(module, "GrandMPNativeCounterReady"));
    bool ok = check(entry && read && prepare && shutdown && wrapper && ready, "real ASI readiness exports");
    if (!ok) { FreeLibrary(module); return false; }
    ok &= check(ready->state == 0 && ready->residencyConfirmed == 0, "UNINITIALIZED without PIN");
    if (scenario == L"asi_not_requested") {
        ok &= check(prepare() == 0 && ready->state == 0 && ready->wrapper == 0,
            "deferred requires request and does not partially publish");
        FreeLibrary(module);
        return ok;
    }
    entry();
    ok &= check(ready->state == 1 && ready->residencyConfirmed == 0 && ready->wrapper == 0,
        "InitializeASI requests only, no PIN or READY");
    if (scenario == L"asi_shutdown_race") {
        std::atomic<bool> start{false};
        std::atomic<unsigned> prepared{0};
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < 16; ++i) {
            threads.emplace_back([&] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                entry();
                if (prepare()) ++prepared;
            });
            threads.emplace_back([&] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                shutdown();
            });
        }
        start.store(true, std::memory_order_release);
        for (auto& thread : threads) thread.join();
        ok &= check(prepared <= 1 && ready->state == 3 && ready->shutdownLogical == 1 &&
            prepare() == 0 && read() == 0, "concurrent shutdown closes preparation permanently");
        const bool resident = ready->residencyConfirmed != 0;
        // No dispatch source is registered. An admitted wrapper remains valid
        // under PIN even if shutdown wins between residence and READY binding.
        if (ready->originalTarget) {
            wrapper();
            ok &= check(originalCallCount == 1 && read() == 0,
                "racing shutdown cannot leave an enabled counter");
        }
        wchar_t path[32768]{};
        GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        ok &= check(FreeLibrary(module) != FALSE, "release racing ASI reference");
        ok &= check((GetModuleHandleW(path) != nullptr) == resident,
            "confirmed residence matches real load state after shutdown race");
        return ok;
    }
    if (scenario == L"asi_partial") {
        auto* bytes = reinterpret_cast<unsigned char*>(&GrandMPNativeCounterHostCallsite);
        DWORD old = 0, temporary = 0;
        if (!check(VirtualProtect(bytes, 5, PAGE_EXECUTE_READWRITE, &old) != FALSE,
            "change controlled host bytes for rejection test")) return false;
        const unsigned char original = bytes[0];
        bytes[0] = 0xE9;
        VirtualProtect(bytes, 5, old, &temporary);
        FlushInstructionCache(GetCurrentProcess(), bytes, 5);
        ok &= check(prepare() == 0 && ready->state == 4 && ready->residencyConfirmed == 0 &&
            ready->wrapper == 0 && prepare() == 0, "mismatched bytes terminal FAILED before PIN");
        VirtualProtect(bytes, 5, PAGE_EXECUTE_READWRITE, &temporary);
        bytes[0] = original;
        VirtualProtect(bytes, 5, old, &temporary);
        FlushInstructionCache(GetCurrentProcess(), bytes, 5);
        entry();
        ok &= check(ready->state == 4, "duplicate entry cannot revive failed state");
        FreeLibrary(module);
        return ok;
    }
    if (scenario == L"asi_shutdown") {
        shutdown(); shutdown(); entry();
        ok &= check(ready->state == 3 && ready->shutdownLogical == 1 && prepare() == 0 &&
            ready->residencyConfirmed == 0, "shutdown before preparation is final and idempotent");
        FreeLibrary(module);
        return ok;
    }
    std::atomic<unsigned> prepared{0};
    if (scenario == L"asi_concurrent") {
        std::vector<std::thread> threads;
        for (unsigned i = 0; i < 16; ++i)
            threads.emplace_back([&] { entry(); if (prepare()) ++prepared; });
        for (auto& thread : threads) thread.join();
    } else if (prepare()) ++prepared;
    ok &= check(prepared == 1 && ready->state == 2 && ready->residencyConfirmed == 1 &&
        ready->wrapperCapable == 1 && ready->installed == 0 && ready->originalTarget ==
        reinterpret_cast<std::uint32_t>(&GrandMPNativeCounterHostOriginal), "one complete READY publication");
    if (!ok) return false;
    std::uint32_t beforeEsp = 0, afterEsp = 0, eaxValue = 0, flags = 0;
    const std::uint32_t xmmPattern[4]{1, 2, 3, 4};
    std::uint32_t xmmAfter[4]{};
    double x87After = 0;
    __asm {
        movdqu xmm0, xmmPattern
        fld1
        mov beforeEsp, esp
        call dword ptr [wrapper]
        mov eaxValue, eax
        pushfd
        pop flags
        mov afterEsp, esp
        movdqu xmmAfter, xmm0
        fstp qword ptr [x87After]
    }
    ok &= check(beforeEsp == afterEsp && eaxValue == 0x11223344 && (flags & 0x8D5) == 0x44 &&
        std::equal(std::begin(xmmPattern), std::end(xmmPattern), std::begin(xmmAfter)) &&
        x87After == 1.0 && originalCallCount == 1 && read() == 1,
        "same ASI wrapper forwards once and preserves stack flags integer x87 SSE state");
    shutdown(); shutdown(); entry(); wrapper();
    ok &= check(ready->state == 3 && read() == 1 && originalCallCount == 2 && prepare() == 0,
        "logical shutdown forwards original but cannot count or reactivate");
    wchar_t path[32768]{};
    GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    ok &= check(FreeLibrary(module) != FALSE && GetModuleHandleW(path) != nullptr,
        "real ASI PIN keeps wrapper and descriptor resident after releasing load ref");
    wrapper();
    ok &= check(originalCallCount == 3 && read() == 1, "resident code remains callable after logical shutdown");
    std::printf("native_counter %ls %s READY=1 PIN=1 original=%ld count=%llu\n", scenario.data(),
        ok ? "PASS" : "FAIL", originalCallCount, static_cast<unsigned long long>(read()));
    return ok;
#else
    (void)module; (void)scenario; (void)entry; (void)read;
    return false;
#endif
}

bool asyncCase(HMODULE module, std::wstring_view scenario, Entry entry, Read read) {
#if defined(_M_IX86)
    using Configure = int(__cdecl*)(HANDLE, HANDLE, unsigned) noexcept;
    const auto configure = reinterpret_cast<Configure>(GetProcAddress(module,
        "GrandMPNativeCounterConfigureAsyncTest"));
    const auto shutdown = reinterpret_cast<Entry>(GetProcAddress(module, "GrandMPNativeCounterShutdown"));
    auto* ready = reinterpret_cast<grandmp::windows::NativeCounterReadyV1*>(
        GetProcAddress(module, "GrandMPNativeCounterReady"));
    bool ok = check(entry && read && shutdown && ready, "async ASI exports");
    if (!ok) { FreeLibrary(module); return false; }
    wchar_t path[32768]{};
    GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    HANDLE gate = nullptr, started = nullptr;
    const bool gated = scenario == L"async_unload" || scenario == L"async_shutdown" ||
        scenario == L"async_pin_failure";
    if (gated) {
        gate = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        started = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        ok &= check(configure && gate && started && configure(gate, started,
            scenario == L"async_pin_failure" ? 2 : 0) == 1, "configure lab-only worker gate");
    } else if (scenario == L"async_thread_failure" || scenario == L"async_reference_failure") {
        ok &= check(configure && configure(nullptr, nullptr,
            scenario == L"async_thread_failure" ? 1 : 3) == 1,
            "inject lab-only scheduling failure");
    }
    if (!ok) {
        if (gate) CloseHandle(gate);
        if (started) CloseHandle(started);
        FreeLibrary(module);
        return false;
    }
    SetEnvironmentVariableW(L"GRANDMP_NATIVE_COUNTER_ASYNC_READY", L"1");
    if (scenario == L"async_concurrent") {
        std::vector<std::thread> callers;
        for (unsigned i = 0; i < 32; ++i) callers.emplace_back([&] { entry(); });
        for (auto& caller : callers) caller.join();
    } else entry();
    SetEnvironmentVariableW(L"GRANDMP_NATIVE_COUNTER_ASYNC_READY", nullptr);
    bool released = false;
    if (scenario == L"async_release") {
        // Real ASI, no auxiliary preparation, gate or observer reference. The
        // caller releases its LoadLibrary reference immediately after entry.
        ok &= check(FreeLibrary(module) != FALSE, "release immediately after InitializeASI returns");
        released = true;
    }
    if (gated) {
        ok &= check(WaitForSingleObject(started, 3000) == WAIT_OBJECT_0,
            "InitializeASI returns and worker starts without caller wait");
        if (scenario == L"async_unload") {
            ok &= check(FreeLibrary(module) != FALSE, "loader immediately releases original reference");
            released = true;
            ok &= check(GetModuleHandleW(path) != nullptr && ready->residencyConfirmed == 0,
                "scheduled worker reference retains code BEFORE PIN");
            // A separate observer reference protects DATA polling even if a
            // later failure makes the worker release its scheduling reference.
            HMODULE observer = nullptr;
            ok &= check(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                reinterpret_cast<LPCWSTR>(ready), &observer) != FALSE && observer == module,
                "read-only observer owns its polling lifetime");
            if (observer == module) released = false;
        }
        if (scenario == L"async_shutdown") shutdown();
        SetEvent(gate);
    }
    grandmp::windows::NativeCounterReadyV1 snapshot{};
    const auto state = [&] {
        if (scenario == L"async_release") {
            SIZE_T bytes = 0;
            // A failed worker could unload the module. Never dereference its
            // DATA or call its exports until a complete READY/PIN snapshot.
            if (!ReadProcessMemory(GetCurrentProcess(), ready, &snapshot, sizeof(snapshot), &bytes) ||
                bytes != sizeof(snapshot)) return std::uint32_t{0xFFFFFFFF};
            return snapshot.state;
        }
        return std::atomic_ref(ready->state).load(std::memory_order_acquire);
    };
    const DWORD target = scenario == L"async_shutdown" ? 3 :
        (scenario == L"async_thread_failure" || scenario == L"async_reference_failure" ||
            scenario == L"async_pin_failure") ? 4 : 2;
    const ULONGLONG deadline = GetTickCount64() + 4000;
    while (state() != target && GetTickCount64() < deadline) Sleep(1);
    ok &= check(state() == target, "async reaches bounded expected terminal state");
    if (scenario == L"async_release" &&
        (!ok || snapshot.state != 2 || snapshot.residencyConfirmed != 1 ||
            snapshot.processId != GetCurrentProcessId() || snapshot.moduleBase !=
            reinterpret_cast<std::uint32_t>(module))) {
        // No reference remains under host ownership. Returning without calling
        // stale exports is mandatory if the worker did not prove residency.
        std::puts("native_counter async_release FAIL no confirmed READY/PIN");
        return false;
    }
    if (target == 2) {
        ok &= check(ready->magic == grandmp::windows::nativeCounterReadyMagic &&
            ready->interfaceVersion == 1 && ready->size == 80 &&
            ready->processId == GetCurrentProcessId() && ready->moduleBase ==
            reinterpret_cast<std::uint32_t>(module) && ready->moduleSize > 0 &&
            ready->residencyConfirmed == 1 && ready->wrapper != 0 &&
            ready->counterAddress != 0 && ready->descriptorAddress ==
            reinterpret_cast<std::uint32_t>(ready) && ready->threadId != GetCurrentThreadId() &&
            ready->originalTarget == 0 && ready->callsite == 0 && ready->wrapperCapable == 0 &&
            ready->installed == 0 && ready->shutdownLogical == 0 && read() == 0,
            "complete READY/PIN on worker, no gameplay binding or counter activation");
        entry();
        ok &= check(state() == 2, "duplicate InitializeASI does not regress async READY");
        if (scenario == L"async_observe") {
            wchar_t eventName[256]{};
            const DWORD length = GetEnvironmentVariableW(L"GRANDMP_COUNTER_OBSERVER_EXIT", eventName, 256);
            HANDLE exitEvent = length && length < 256 ? OpenEventW(SYNCHRONIZE, FALSE, eventName) : nullptr;
            std::printf("ASYNC_OBSERVE PID=%lu READY=1 PIN=1\n", GetCurrentProcessId());
            std::fflush(stdout);
            ok &= check(exitEvent && WaitForSingleObject(exitEvent, 10000) == WAIT_OBJECT_0,
                "external read-only observer completes within bound");
            if (exitEvent) CloseHandle(exitEvent);
        }
        shutdown(); shutdown(); entry();
        ok &= check(state() == 3 && ready->shutdownLogical == 1 && read() == 0,
            "async shutdown is logical final idempotent and keeps counter zero");
        if (!released) { ok &= check(FreeLibrary(module) != FALSE, "release non-cooperative loader reference"); released = true; }
        ok &= check(GetModuleHandleW(path) != nullptr, "PIN retains real ASI after loader reference release");
    } else {
        ok &= check(ready->wrapper == 0 && ready->wrapperCapable == 0 && ready->installed == 0 &&
            read() == 0, "failure/shutdown cannot publish partial READY");
        if (!released) { FreeLibrary(module); released = true; }
        const ULONGLONG unloadDeadline = GetTickCount64() + 3000;
        while (GetModuleHandleW(path) && GetTickCount64() < unloadDeadline) Sleep(1);
        ok &= check(GetModuleHandleW(path) == nullptr, "failed/cancelled worker releases its scheduling reference");
    }
    if (gate) CloseHandle(gate);
    if (started) CloseHandle(started);
    std::printf("native_counter %ls %s READY=%lu no_aux_prepare=1\n", scenario.data(),
        ok ? "PASS" : "FAIL", target);
    return ok;
#else
    (void)module; (void)scenario; (void)entry; (void)read;
    return false;
#endif
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return 2;
    const std::wstring_view scenario{argv[2]};
    if (scenario == L"gate") return gateCase() ? 0 : 3;
    if (scenario == L"attach_no_module") {
        (void)std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::puts("attach_target_ready=1");
        (void)std::getchar();
        return IsDebuggerPresent() ? 8 : 0;
    }
    HMODULE module = LoadLibraryW(argv[1]);
    if (!check(module != nullptr, "LoadLibrary lab")) return 4;
    const auto entry = reinterpret_cast<Entry>(GetProcAddress(module, "InitializeASI"));
    const auto prepare = reinterpret_cast<Prepare>(GetProcAddress(module, "GrandMPNativeCounterPrepareHost"));
    const auto dispatch = reinterpret_cast<Entry>(GetProcAddress(module, "GrandMPNativeCounterDispatchHost"));
    const auto disable = reinterpret_cast<Entry>(GetProcAddress(module, "GrandMPNativeCounterDisableHost"));
    const auto read = reinterpret_cast<Read>(GetProcAddress(module, "GrandMPNativeCounterRead"));
    const auto status = reinterpret_cast<Status>(GetProcAddress(module, "GrandMPNativeCounterInstallationStatus"));
    if (scenario.starts_with(L"async_")) return asyncCase(module, scenario, entry, read) ? 0 : 10;
    if (scenario.starts_with(L"asi_")) return asiCase(module, scenario, entry, read) ? 0 : 9;
    if (scenario == L"inert") {
        bool inert = check(entry && read && status, "inert ASI exports");
        inert &= check(!prepare && !dispatch && !disable, "lab activation absent from ASI exports");
        auto* ready = reinterpret_cast<grandmp::windows::NativeCounterReadyV1*>(
            GetProcAddress(module, "GrandMPNativeCounterReady"));
        inert &= check(ready && !GetProcAddress(module, "GrandMPNativeCounterPrepareTransaction"),
            "READY inspectable but arbitrary-address lab bind absent");
        if (entry && read && status) {
            for (unsigned i = 0; i < 16; ++i) entry();
            inert &= check(read() == 0 && status() == 3u, "ASI remains inert after InitializeASI");
            inert &= check(ready && ready->state == 1 && ready->residencyConfirmed == 0 &&
                ready->wrapper == 0, "REQUESTED is not READY or PIN");
        }
        wchar_t path[32768]{};
        const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
        inert &= check(length != 0 && length < std::size(path), "inert ASI path");
        inert &= check(FreeLibrary(module) != FALSE, "release inert ASI");
        inert &= check(GetModuleHandleW(path) == nullptr, "no retention before callback activation");
        return inert ? 0 : 5;
    }
    if (!check(entry && prepare && dispatch && disable && read && status, "exports")) {
        FreeLibrary(module);
        return 5;
    }
    if (scenario == L"attach_target" || scenario == L"attach_invalid" || scenario == L"attach_counter")
        return attachTarget(module, entry, prepare, read, scenario == L"attach_invalid", scenario == L"attach_counter") ? 0 : 8;
    if (scenario == L"debug_target" || scenario == L"debug_target_restore")
        return debugTarget(module, entry, prepare, disable, read, scenario == L"debug_target_restore") ? 0 : 8;
    entry();
    dispatch();
    bool ok = check(read() == 0 && status() == 3u, "default disabled and GTA blocked");
    if (scenario == L"shutdown_race") {
        std::atomic<bool> start{false};
        std::atomic<unsigned> prepared{0};
        std::vector<std::thread> racing;
        for (unsigned i = 0; i < 16; ++i) {
            racing.emplace_back([&] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                if (prepare(0)) ++prepared;
            });
            racing.emplace_back([&] {
                while (!start.load(std::memory_order_acquire)) std::this_thread::yield();
                disable();
            });
        }
        start.store(true, std::memory_order_release);
        for (auto& thread : racing) thread.join();
        ok &= check(prepared <= 1, "racing initialization at most once");
        for (unsigned i = 0; i < 100; ++i) dispatch();
        ok &= check(read() == 0 && prepare(0) == 0, "concurrent disable is final");
        FreeLibrary(module);
        return ok ? 0 : 6;
    }
    if (scenario == L"cancel") {
        disable();
        ok &= check(prepare(0) == 0, "disable before prepare is final");
        dispatch();
        ok &= check(read() == 0, "cancelled bootstrap cannot count");
        FreeLibrary(module);
        return ok ? 0 : 6;
    }
    if (scenario == L"partial") {
        ok &= check(prepare(1) == 0, "injected failure before PIN");
        dispatch();
        ok &= check(read() == 0 && prepare(0) == 0, "failed bootstrap stays inert");
        CounterSource source;
        ok &= check(!source.install(dispatch, true), "publication failure");
        source.update();
        ok &= check(read() == 0 && source.originals == 1, "failed publication preserves original");
        ok &= check(source.shutdown(), "failure cleanup");
        FreeLibrary(module);
        std::puts("native_counter partial PASS");
        return ok ? 0 : 6;
    }
    std::atomic<unsigned> initialized{0};
    std::vector<std::thread> callers;
    for (unsigned i = 0; i < 16; ++i)
        callers.emplace_back([&] { entry(); if (prepare(0)) ++initialized; });
    for (auto& thread : callers) thread.join();
    ok &= check(initialized == 1, "one deferred PIN initializer");
    CounterSource source;
    ok &= check(source.install(dispatch), "cooperative publication");
    ok &= check(!source.install(dispatch), "duplicate publication refused");
    if (scenario == L"concurrent" || scenario == L"shutdown") {
        callers.clear();
        for (unsigned i = 0; i < 8; ++i)
            callers.emplace_back([&] { for (unsigned j = 0; j < 2000; ++j) source.update(); });
        if (scenario == L"shutdown") ok &= check(source.shutdown(), "concurrent quiescence");
        for (auto& thread : callers) thread.join();
        if (scenario == L"concurrent") ok &= check(read() == 16000, "all concurrent callback increments");
        ok &= check(source.originals == 16000, "all originals preserved");
    } else if (scenario == L"reentrant") {
        CounterSource recursive;
        reentrantSource = &recursive;
        reentrantTarget = dispatch;
        ok &= check(recursive.install(reentrantCallback), "publish reentrant callback");
        recursive.recursiveOnce = true;
        recursive.update();
        ok &= check(recursive.originals == 2 && read() == 1, "reentrant update forwards without nested callback");
        ok &= check(nestedShutdownRefused, "shutdown during callback refuses self-wait");
        ok &= check(recursive.shutdown(), "external shutdown after reentrant refusal");
    } else if (scenario == L"once" || scenario == L"forwarding" || scenario == L"residence") {
        for (unsigned i = 0; i < 100; ++i) source.update();
        ok &= check(source.originals == 100 && read() == 100, "exactly one original and counter per update");
    } else {
        ok = false;
    }
    ok &= check(source.shutdown() && source.shutdown(), "idempotent source shutdown");
    if (scenario == L"shutdown") {
        CounterSource heldSource;
        heldTarget = dispatch;
        ok &= check(heldSource.install(heldCallback), "publish held callback");
        std::thread active([&] { heldSource.update(); });
        callbackEntered.acquire();
        std::atomic<bool> shutdownReturned{false};
        std::thread stopping([&] { (void)heldSource.shutdown(); shutdownReturned = true; });
        while (!heldSource.stopped()) std::this_thread::yield();
        ok &= check(!shutdownReturned, "shutdown waits for in-flight callback");
        releaseCallback.release();
        active.join();
        stopping.join();
        ok &= check(shutdownReturned, "shutdown completes after callback returns");
    }
    disable();
    const auto stoppedCount = read();
    source.update();
    dispatch();
    ok &= check(read() == stoppedCount, "logical shutdown suppresses counting after quiescence");
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(module, path, static_cast<DWORD>(std::size(path)));
    ok &= check(length != 0 && length < std::size(path), "module path");
    ok &= check(FreeLibrary(module) != FALSE, "release host reference");
    ok &= check(GetModuleHandleW(path) != nullptr, "PIN retains callback code after release");
    // Source is quiescent and the module is pinned. No unload is attempted and
    // no API pointers are called again. OS termination owns process cleanup.
    std::puts("native_counter host PASS");
    return ok ? 0 : 7;
}
