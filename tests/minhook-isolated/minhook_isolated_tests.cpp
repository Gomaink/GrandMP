#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <MinHook.h>

#include <atomic>
#include <cstdio>
#include <thread>
#include <vector>

namespace {
using Target = int(__cdecl*)(int);
using SetOriginal = void(__cdecl*)(void*);
using CallbackCount = unsigned(__cdecl*)();

int fail(const char* stage, MH_STATUS status = MH_OK) {
    std::fprintf(stderr, "%s: %s\n", stage, MH_StatusToString(status));
    return 1;
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc != 3) return fail("expected target and detour DLL paths");
    const HMODULE targetModule = LoadLibraryW(argv[1]);
    const HMODULE detourModule = LoadLibraryW(argv[2]);
    if (!targetModule || !detourModule) return fail("LoadLibrary");
    const auto target = reinterpret_cast<Target>(GetProcAddress(targetModule, "ProbeTarget"));
    const auto detour = GetProcAddress(detourModule, "ProbeDetour");
    const auto setOriginal = reinterpret_cast<SetOriginal>(GetProcAddress(detourModule, "ProbeSetOriginal"));
    const auto callbackCount = reinterpret_cast<CallbackCount>(GetProcAddress(detourModule, "ProbeCallbackCount"));
    if (!target || !detour || !setOriginal || !callbackCount || target(5) != 12)
        return fail("unhooked target/exports");

    MH_STATUS status = MH_Initialize();
    if (status != MH_OK) return fail("MH_Initialize", status);
    void* trampoline = nullptr;
    status = MH_CreateHook(nullptr, detour, &trampoline);
    if (status != MH_ERROR_NOT_EXECUTABLE || target(5) != 12)
        return fail("invalid target must fail without patch", status);
    status = MH_CreateHook(reinterpret_cast<void*>(target), detour, &trampoline);
    if (status != MH_OK || trampoline == nullptr) return fail("MH_CreateHook", status);
    if (MH_CreateHook(reinterpret_cast<void*>(target), detour, nullptr) != MH_ERROR_ALREADY_CREATED)
        return fail("duplicate create must fail");
    setOriginal(trampoline);
    status = MH_EnableHook(reinterpret_cast<void*>(target));
    if (status != MH_OK) return fail("MH_EnableHook", status);
    if (MH_EnableHook(reinterpret_cast<void*>(target)) != MH_ERROR_ENABLED)
        return fail("duplicate enable must fail");
    if (target(5) != 112 || callbackCount() != 1)
        return fail("detour must call original trampoline");

    std::atomic<unsigned> errors{0};
    std::vector<std::thread> workers;
    for (int i = 0; i < 4; ++i) {
        workers.emplace_back([&] {
            for (int call = 0; call < 1000; ++call) {
                if (target(5) != 112) errors.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }
    for (auto& worker : workers) worker.join();
    if (errors.load() != 0 || callbackCount() != 4001)
        return fail("concurrent enabled calls");

    status = MH_DisableHook(reinterpret_cast<void*>(target));
    if (status != MH_OK || target(5) != 12) return fail("MH_DisableHook", status);
    if (MH_DisableHook(reinterpret_cast<void*>(target)) != MH_ERROR_DISABLED)
        return fail("duplicate disable must fail");
    status = MH_RemoveHook(reinterpret_cast<void*>(target));
    if (status != MH_OK || target(5) != 12) return fail("MH_RemoveHook", status);
    status = MH_Uninitialize();
    if (status != MH_OK) return fail("MH_Uninitialize", status);
    if (!FreeLibrary(detourModule) || GetModuleHandleW(argv[2]) != nullptr)
        return fail("detour DLL did not unload");
    for (int i = 0; i < 1000; ++i) {
        if (target(5) != 12) return fail("callback after detour unload");
    }
    if (!FreeLibrary(targetModule)) return fail("target DLL unload");
    std::printf("MinHook isolated: original=12 detour=112 callbacks=4001 workers=4 removal=ok unload=ok\n");
    return 0;
}
