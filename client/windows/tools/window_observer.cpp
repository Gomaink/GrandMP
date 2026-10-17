#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <iterator>
#include <limits>
#include <string>
#include <vector>

namespace {
struct ProcessRecord {
    DWORD pid;
};

struct Options {
    DWORD selectedPid = 0;
    DWORD initializeThread = 0;
};

struct Enumeration {
    const Options* options;
    const std::vector<ProcessRecord>* processes;
    unsigned windows = 0;
    unsigned mainCandidates = 0;
};

bool parseId(const wchar_t* text, DWORD& result) {
    if (text == nullptr || *text == L'\0' || *text == L'-' || *text == L'+') return false;
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::wcstoul(text, &end, 10);
    if (errno == ERANGE || *end != L'\0' || parsed == 0 ||
        parsed > std::numeric_limits<DWORD>::max()) return false;
    result = static_cast<DWORD>(parsed);
    return true;
}

bool hasProcess(const std::vector<ProcessRecord>& processes, DWORD pid) {
    for (const auto& process : processes) {
        if (process.pid == pid) return true;
    }
    return false;
}

BOOL CALLBACK recordWindow(HWND window, LPARAM parameter) {
    auto& context = *reinterpret_cast<Enumeration*>(parameter);
    DWORD pid = 0;
    const DWORD tid = GetWindowThreadProcessId(window, &pid);
    if (tid == 0 || !hasProcess(*context.processes, pid) ||
        (context.options->selectedPid != 0 && context.options->selectedPid != pid)) return TRUE;

    const HWND owner = GetWindow(window, GW_OWNER);
    const bool visible = IsWindowVisible(window) != FALSE;
    const bool toolWindow = (GetWindowLongPtrW(window, GWL_EXSTYLE) & WS_EX_TOOLWINDOW) != 0;
    RECT rect{};
    const bool sized = GetWindowRect(window, &rect) && rect.right > rect.left && rect.bottom > rect.top;
    const bool mainCandidate = visible && owner == nullptr && !toolWindow && sized;
    ++context.windows;
    if (mainCandidate) ++context.mainCandidates;

    wchar_t className[256]{};
    wchar_t title[256]{};
    GetClassNameW(window, className, static_cast<int>(std::size(className)));
    GetWindowTextW(window, title, static_cast<int>(std::size(title)));
    std::wprintf(L"  HWND=0x%llX PID=%lu TID=%lu tipo=%ls visivel=%ls owner=0x%llX class=%ls title=%ls",
                 static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(window)),
                 static_cast<unsigned long>(pid), static_cast<unsigned long>(tid),
                 mainCandidate ? L"candidata-principal" : L"auxiliar-ou-oculta",
                 visible ? L"sim" : L"nao",
                 static_cast<unsigned long long>(reinterpret_cast<std::uintptr_t>(owner)),
                 className, title);
    if (context.options->initializeThread != 0) {
        std::wprintf(L" match-InitializeASI=%ls",
                     tid == context.options->initializeThread ? L"sim" : L"nao");
    }
    std::wprintf(L"\n");
    return TRUE;
}

bool findGameProcesses(std::vector<ProcessRecord>& processes) {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot, &entry)) {
        CloseHandle(snapshot);
        return false;
    }
    do {
        if (_wcsicmp(entry.szExeFile, L"gta_sa.exe") == 0) {
            processes.push_back({entry.th32ProcessID});
        }
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return true;
}

void usage() {
    std::wprintf(L"Uso: grandmp_window_observer [--pid PID] [--init-tid TID]\n"
                 L"Leia PID e TID da linha InitializeASI em GrandMP.log.\n");
}
}

int wmain(int argc, wchar_t** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        if (std::wcscmp(argv[i], L"--help") == 0) {
            usage();
            return 0;
        }
        if (i + 1 >= argc) {
            usage();
            return 2;
        }
        DWORD value = 0;
        if (!parseId(argv[++i], value)) {
            std::fwprintf(stderr, L"PID/TID invalido.\n");
            return 2;
        }
        if (std::wcscmp(argv[i - 1], L"--pid") == 0 && options.selectedPid == 0)
            options.selectedPid = value;
        else if (std::wcscmp(argv[i - 1], L"--init-tid") == 0 && options.initializeThread == 0)
            options.initializeThread = value;
        else {
            usage();
            return 2;
        }
    }

    std::vector<ProcessRecord> processes;
    if (!findGameProcesses(processes)) {
        std::fwprintf(stderr, L"Falha ao enumerar processos (erro %lu).\n", GetLastError());
        return 1;
    }
    if (processes.empty()) {
        std::wprintf(L"Nenhum processo gta_sa.exe encontrado.\n");
        return 0;
    }
    if (options.selectedPid != 0 && !hasProcess(processes, options.selectedPid)) {
        std::fwprintf(stderr, L"PID %lu nao corresponde a gta_sa.exe em execucao.\n",
                      static_cast<unsigned long>(options.selectedPid));
        return 1;
    }
    for (const auto& process : processes) {
        if (options.selectedPid == 0 || options.selectedPid == process.pid)
            std::wprintf(L"gta_sa.exe PID=%lu\n", static_cast<unsigned long>(process.pid));
    }
    if (options.initializeThread != 0)
        std::wprintf(L"InitializeASI TID informado=%lu\n",
                     static_cast<unsigned long>(options.initializeThread));

    Enumeration context{&options, &processes};
    if (!EnumWindows(recordWindow, reinterpret_cast<LPARAM>(&context))) {
        std::fwprintf(stderr, L"Falha ao enumerar janelas (erro %lu).\n", GetLastError());
        return 1;
    }
    std::wprintf(L"Janelas=%u candidatas-principais=%u\n", context.windows, context.mainCandidates);
    std::wprintf(L"Despacho de mensagens e thread segura de atualizacao do jogo: nao observados.\n");
    return 0;
}
