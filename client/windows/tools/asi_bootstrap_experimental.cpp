#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "grandmp/windows/asi_bootstrap.hpp"

namespace {

// Zero-initialized objects only. No runtime object or teardown is attached to
// DLL_PROCESS_DETACH; the retained module reference is process-lifetime.
HMODULE moduleHandle = nullptr;
unsigned char moduleAnchor = 0;
grandmp::windows::AsiBootstrapController bootstrap;
HMODULE lifetimeReference = nullptr;

}  // namespace

extern "C" BOOL WINAPI DllMain(HINSTANCE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) moduleHandle = module;
    return TRUE;
}

extern "C" void __cdecl InitializeASI() noexcept {
    // Deliberately records intent only. UAL 7.7.0's call context is not proven
    // to be outside the loader lock, so no allocation, file I/O, thread, PIN,
    // logging, or game-dependent work is started here.
    (void)bootstrap.requestEntry();
}

extern "C" int __cdecl GrandMPBootstrapRunDeferred() noexcept {
    if (bootstrap.beginDeferred() != grandmp::windows::AsiBootstrapResult::accepted) return 0;

    HMODULE acquired = nullptr;
    const auto ownAddress = reinterpret_cast<LPCWSTR>(&moduleAnchor);
    const BOOL retained = GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                                               ownAddress, &acquired);
    if (!retained || acquired != moduleHandle) {
        if (retained && acquired) FreeLibrary(acquired);
        (void)bootstrap.publishResidence(false);
        return 0;
    }

    // Keep this owning reference for the rest of the process. There is no
    // release export and no FreeLibrary from DllMain. This is reachable only
    // through an explicit deferred call, never from InitializeASI.
    lifetimeReference = acquired;
    return bootstrap.publishResidence(true) == grandmp::windows::AsiBootstrapResult::completed ? 1 : 0;
}

extern "C" unsigned int __cdecl GrandMPBootstrapGetState() noexcept {
    return static_cast<unsigned int>(bootstrap.state());
}

extern "C" int __cdecl GrandMPBootstrapDisableLogically() noexcept {
    return bootstrap.disableLogically() == grandmp::windows::AsiBootstrapResult::disabled ? 1 : 0;
}
