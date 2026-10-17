#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>

namespace {
using Start = BOOL(__cdecl*)(HWND, UINT);
using Enable = void(__cdecl*)(BOOL);
using Stop = BOOL(__cdecl*)();
using Snapshot = void(__cdecl*)(DWORD*, DWORD*, unsigned*, unsigned*);

LRESULT CALLBACK windowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam) {
    return DefWindowProcW(window, message, wParam, lParam);
}

void pump(HWND window, DWORD durationMs) {
    const DWORD start = GetTickCount();
    MSG message{};
    while (GetTickCount() - start < durationMs) {
        while (PeekMessageW(&message, window, 0, 0, PM_REMOVE)) DispatchMessageW(&message);
        Sleep(2);
    }
}

int fail(const char* message) {
    std::fprintf(stderr, "%s\n", message);
    return 1;
}
}

int main(int argc, char** argv) {
    if (argc != 2) return fail("expected probe DLL path");
    const HMODULE probe = LoadLibraryA(argv[1]);
    if (!probe) return fail("LoadLibrary failed");
    const auto start = reinterpret_cast<Start>(GetProcAddress(probe, "ProbeStart"));
    const auto enable = reinterpret_cast<Enable>(GetProcAddress(probe, "ProbeEnable"));
    const auto stop = reinterpret_cast<Stop>(GetProcAddress(probe, "ProbeStop"));
    const auto snapshot = reinterpret_cast<Snapshot>(GetProcAddress(probe, "ProbeSnapshot"));
    if (!start || !enable || !stop || !snapshot) return fail("probe exports missing");

    const HINSTANCE instance = GetModuleHandleW(nullptr);
    WNDCLASSW klass{};
    klass.lpfnWndProc = windowProc;
    klass.hInstance = instance;
    klass.lpszClassName = L"GrandMP_TimerProbeHost";
    if (!RegisterClassW(&klass)) return fail("RegisterClass failed");
    HWND window = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0,
                                 HWND_MESSAGE, nullptr, instance, nullptr);
    if (!window) return fail("CreateWindowEx failed");
    DWORD process = 0;
    if (GetWindowThreadProcessId(window, &process) != GetCurrentThreadId() ||
        process != GetCurrentProcessId()) return fail("window owner mismatch");
    if (!start(window, 40)) return fail("SetTimer failed");
    pump(window, 260);
    DWORD owner = 0, callback = 0;
    unsigned total = 0, active = 0;
    snapshot(&owner, &callback, &total, &active);
    if (total < 2 || active != total || owner != GetCurrentThreadId() || callback != owner)
        return fail("timer did not dispatch on owner thread");

    enable(FALSE);
    const unsigned beforeDisabled = total;
    pump(window, 130);
    snapshot(nullptr, nullptr, &total, &active);
    if (total <= beforeDisabled || active != beforeDisabled)
        return fail("logical disable did not preserve callback without work");

    // Force an actual WM_TIMER to be queued, then stop before dispatching it.
    MSG pending{};
    bool queued = false;
    for (int i = 0; i < 100 && !queued; ++i) {
        Sleep(5);
        queued = PeekMessageW(&pending, window, WM_TIMER, WM_TIMER, PM_NOREMOVE) != FALSE;
    }
    if (!queued || pending.lParam == 0) return fail("no pending TimerProc message");
    const unsigned beforeStop = total;
    if (!stop()) return fail("KillTimer failed");
    if (!PeekMessageW(&pending, window, WM_TIMER, WM_TIMER, PM_REMOVE))
        return fail("KillTimer unexpectedly removed pending WM_TIMER");
    DispatchMessageW(&pending);
    snapshot(nullptr, nullptr, &total, &active);
    // The queued message remains, but this OS may suppress TimerProc dispatch
    // after KillTimer. Neither outcome is a guarantee for unloading a DLL.
    if (total < beforeStop || total > beforeStop + 1 || active != beforeDisabled)
        return fail("unexpected pending-message callback state");

    pump(window, 130);
    unsigned afterDrain = 0;
    snapshot(nullptr, nullptr, &afterDrain, nullptr);
    if (afterDrain != total) return fail("callback occurred after timer stopped");
    if (!FreeLibrary(probe)) return fail("FreeLibrary failed");
    if (GetModuleHandleA("grandmp_timer_probe.dll") != nullptr)
        return fail("probe DLL remained loaded after drained timer was stopped");
    pump(window, 80);
    DestroyWindow(window);
    UnregisterClassW(klass.lpszClassName, instance);
    std::printf("owner=%lu callback=%lu active=%u disabled=%u pending_after_kill=1 late_callback=%u\n",
                static_cast<unsigned long>(owner), static_cast<unsigned long>(callback),
                active, total - active, total - beforeStop);
    return 0;
}
