#include <windows.h>

// Disposable fixture only: observe a real loader-lock-owned call in DllMain.
extern "C" __declspec(dllexport) __declspec(noinline) void LockedEntry() {
    static volatile LONG calls = 0;
    calls = calls + 1;
}

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        LockedEntry();
    }
    return TRUE;
}
