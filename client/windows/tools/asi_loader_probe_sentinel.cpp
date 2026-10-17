#define WIN32_LEAN_AND_MEAN
#include <windows.h>

extern "C" BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }

extern "C" __declspec(dllexport) void __cdecl InitializeASI() noexcept {
    HANDLE marker = CreateFileW(L"ual_probe_initialize_asi.marker", GENERIC_WRITE,
                                FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                                FILE_ATTRIBUTE_NORMAL, nullptr);
    if (marker == INVALID_HANDLE_VALUE) return;
    char record[96]{};
    const int length = wsprintfA(record, "pid=%lu tid=%lu\r\n",
        static_cast<unsigned long>(GetCurrentProcessId()),
        static_cast<unsigned long>(GetCurrentThreadId()));
    DWORD written = 0;
    (void)WriteFile(marker, record, static_cast<DWORD>(length), &written, nullptr);
    CloseHandle(marker);
}
