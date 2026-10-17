#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

int wmain() {
    // The parent starts this process with an isolated working directory that
    // contains only the verified UAL proxy and our sentinel ASI.
    HMODULE loader = LoadLibraryW(L".\\dinput8.dll");
    if (!loader) {
        std::fprintf(stderr, "LoadLibraryW dinput8 failed: %lu\n", GetLastError());
        return 2;
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    GetStartupInfoW(&startup); // IAT wrapper is the configured deferred trigger.

    const DWORD attributes = GetFileAttributesW(L"ual_probe_initialize_asi.marker");
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        std::fprintf(stderr, "UAL did not invoke the sentinel (error=%lu)\n", GetLastError());
        return 3;
    }
    std::puts("ual_synthetic_probe=pass trigger=GetStartupInfoW sentinel=InitializeASI");
    // Do not FreeLibrary the proxy: let normal process teardown own the test.
    return 0;
}
