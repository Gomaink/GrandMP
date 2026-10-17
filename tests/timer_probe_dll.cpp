#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdint>

namespace {
HWND ownerWindow = nullptr;
UINT_PTR timerId = 0;
constexpr UINT_PTR requestedId = 0x47504D50;
DWORD ownerThread = 0;
DWORD callbackThread = 0;
unsigned callbackCount = 0;
unsigned enabledCount = 0;
bool enabled = true;

void CALLBACK onTimer(HWND, UINT, UINT_PTR id, DWORD) {
    if (id != requestedId) return;
    callbackThread = GetCurrentThreadId();
    ++callbackCount;
    if (enabled) ++enabledCount;
}
}

extern "C" __declspec(dllexport) BOOL __cdecl ProbeStart(HWND window, UINT intervalMs) {
    if (timerId != 0 || window == nullptr || intervalMs == 0) return FALSE;
    DWORD process = 0;
    const DWORD thread = GetWindowThreadProcessId(window, &process);
    if (thread == 0 || thread != GetCurrentThreadId() || process != GetCurrentProcessId()) return FALSE;
    ownerWindow = window;
    ownerThread = thread;
    enabled = true;
    callbackCount = 0;
    enabledCount = 0;
    callbackThread = 0;
    timerId = SetTimer(window, requestedId, intervalMs, onTimer);
    if (timerId == 0) {
        ownerWindow = nullptr;
        ownerThread = 0;
        return FALSE;
    }
    return TRUE;
}

extern "C" __declspec(dllexport) void __cdecl ProbeEnable(BOOL value) {
    enabled = value != FALSE;
}

extern "C" __declspec(dllexport) BOOL __cdecl ProbeStop() {
    if (timerId == 0) return FALSE;
    const BOOL result = KillTimer(ownerWindow, timerId);
    if (result) {
        timerId = 0;
        ownerWindow = nullptr;
    }
    return result;
}

extern "C" __declspec(dllexport) void __cdecl ProbeSnapshot(DWORD* owner, DWORD* callback,
                                                              unsigned* total, unsigned* active) {
    if (owner) *owner = ownerThread;
    if (callback) *callback = callbackThread;
    if (total) *total = callbackCount;
    if (active) *active = enabledCount;
}
