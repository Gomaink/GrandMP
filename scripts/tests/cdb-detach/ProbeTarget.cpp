#include <windows.h>

#include <fstream>
#include <string>

namespace {
void Mark(const char* name, const char* value) {
    std::ofstream marker(name, std::ios::binary);
    marker << value;
}

LONG WINAPI RecordUnhandledException(EXCEPTION_POINTERS* exception) {
    const auto code = std::to_string(exception->ExceptionRecord->ExceptionCode);
    Mark("unhandled-exception.txt", code.c_str());
    return EXCEPTION_EXECUTE_HANDLER;
}
}

extern "C" __declspec(noinline) void ArmBreakpoint() {
    Mark("armed.txt", "armed");
}

extern "C" __declspec(noinline) void ProbeFunction() {
    Sleep(300);
}

int main() {
    SetUnhandledExceptionFilter(RecordUnhandledException);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
    if (LoadLibraryW(L"ProbeModule.dll") == nullptr) {
        Mark("failed.txt", "LoadLibrary ProbeModule.dll failed");
        return 11;
    }
    const std::string processId = std::to_string(GetCurrentProcessId());
    Mark("ready.txt", processId.c_str());
    Sleep(1200);

    const bool cancelled = GetFileAttributesW(L"cancel.flag") != INVALID_FILE_ATTRIBUTES;
    const bool validationFailed = GetFileAttributesW(L"validation-failed.flag") != INVALID_FILE_ATTRIBUTES;
    if (cancelled) {
        Mark("cancelled.txt", "cancelled");
    } else if (validationFailed) {
        Mark("validation-failed.txt", "rejected before arming");
    } else {
        ArmBreakpoint();
        ProbeFunction();
        Mark("after-function.txt", "alive");
    }

    const HMODULE detachTrigger = LoadLibraryW(L"ProbeDetach.dll");
    if (detachTrigger == nullptr) {
        Mark("failed.txt", "LoadLibrary ProbeDetach.dll failed");
        return 13;
    }

    Mark("after-detach.txt", IsDebuggerPresent() ? "debugger" : "detached");
    ProbeFunction();
    Mark("after-detach-function.txt", "completed");
    Sleep(700);
    Mark("done.txt", "complete");
    Mark("normal-return.txt", "0");
    return 0;
}
