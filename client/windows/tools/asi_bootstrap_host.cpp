#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
#include <vector>

namespace {

using Initialize = void(__cdecl*)();
using RunDeferred = int(__cdecl*)();
using GetState = unsigned int(__cdecl*)();
using Disable = int(__cdecl*)();

bool check(bool condition, const char* message) {
    if (!condition) std::fprintf(stderr, "FAIL: %s (error=%lu)\n", message, GetLastError());
    return condition;
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 2;
    HMODULE module = LoadLibraryW(argv[1]);
    if (!check(module != nullptr, "LoadLibraryW")) return 3;

    const auto initialize = reinterpret_cast<Initialize>(GetProcAddress(module, "InitializeASI"));
    const auto runDeferred = reinterpret_cast<RunDeferred>(GetProcAddress(module, "GrandMPBootstrapRunDeferred"));
    const auto getState = reinterpret_cast<GetState>(GetProcAddress(module, "GrandMPBootstrapGetState"));
    const auto disable = reinterpret_cast<Disable>(GetProcAddress(module, "GrandMPBootstrapDisableLogically"));
    if (!check(initialize && runDeferred && getState && disable, "resolve exports")) {
        FreeLibrary(module);
        return 4;
    }

    // LoadLibrary has returned before any of these calls. This validates only
    // the explicit host-controlled deferred path, not UAL's GTA call context.
    constexpr int callerCount = 16;
    std::vector<std::thread> callers;
    for (int i = 0; i < callerCount; ++i) callers.emplace_back(initialize);
    for (auto& caller : callers) caller.join();
    if (!check(getState() == 1u, "entry request state")) return 5;

    std::atomic<unsigned> initialized{0};
    callers.clear();
    for (int i = 0; i < callerCount; ++i) {
        callers.emplace_back([&] {
            if (runDeferred()) initialized.fetch_add(1, std::memory_order_relaxed);
        });
    }
    for (auto& caller : callers) caller.join();
    if (!check(initialized.load(std::memory_order_relaxed) == 1, "exactly one deferred initializer")) return 6;
    if (!check(getState() == 3u, "resident state")) return 7;
    if (!check(disable() == 1 && getState() == 5u, "logical shutdown")) return 8;

    wchar_t fullPath[32768]{};
    const DWORD pathLength = GetFullPathNameW(argv[1], static_cast<DWORD>(std::size(fullPath)), fullPath, nullptr);
    if (!check(pathLength > 0 && pathLength < std::size(fullPath), "resolve module path")) return 9;
    if (!check(FreeLibrary(module) != FALSE, "release host's LoadLibrary reference")) return 10;
    if (!check(GetModuleHandleW(fullPath) != nullptr, "module remains loaded via process-lifetime reference")) return 11;

    std::puts("bootstrap_host=pass entry=concurrent deferred=once logical_shutdown=pass module_resident=1");
    // No API pointers are used after FreeLibrary(host reference). The retained
    // module reference is intentionally left for OS process teardown.
    return 0;
}
