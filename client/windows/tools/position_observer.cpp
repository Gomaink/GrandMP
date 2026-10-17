#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <bcrypt.h>

#include "grandmp/windows/executable_detection.hpp"
#include "grandmp/windows/observer_network.hpp"
#include "grandmp/windows/position_observer.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <limits>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {

using grandmp::windows::Coordinates;
using grandmp::windows::MemoryReader;
using grandmp::windows::ObserverState;
using grandmp::windows::PollState;
using grandmp::windows::PositionStatus;
using grandmp::windows::ReadResult;

static_assert(grandmp::windows::observerProcessAccess ==
              (PROCESS_VM_READ | PROCESS_QUERY_LIMITED_INFORMATION));
constexpr std::uintptr_t expectedBase = 0x00400000;
std::atomic_bool stopRequested{false};

class Handle {
public:
    explicit Handle(HANDLE value = INVALID_HANDLE_VALUE) noexcept : value_(value) {}
    ~Handle() { if (valid()) CloseHandle(value_); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    [[nodiscard]] bool valid() const noexcept {
        return value_ != nullptr && value_ != INVALID_HANDLE_VALUE;
    }
    [[nodiscard]] HANDLE get() const noexcept { return value_; }
private:
    HANDLE value_;
};

class Algorithm {
public:
    ~Algorithm() { if (value) BCryptCloseAlgorithmProvider(value, 0); }
    BCRYPT_ALG_HANDLE value = nullptr;
};

class Hash {
public:
    ~Hash() { if (value) BCryptDestroyHash(value); }
    BCRYPT_HASH_HANDLE value = nullptr;
};

// Apenas diagnostico; nao cria peds, nao escreve no GTA e nao instala hooks.
class ConsoleScenePreview final : public grandmp::core::IRemotePlayerSceneSink {
public:
    void spawn(const grandmp::core::RemotePose& pose) noexcept override {
        std::fwprintf(stdout, L"[scene] spawn remoto %llu em %.3f %.3f %.3f\n",
            static_cast<unsigned long long>(pose.id), static_cast<double>(pose.x),
            static_cast<double>(pose.y), static_cast<double>(pose.z));
    }
    void move(const grandmp::core::RemotePose& pose) noexcept override {
        // Amostrar, sem imprimir centenas de linhas por segundo.
        if (++updates_ % 5 == 0)
            std::fwprintf(stdout, L"[scene] pose remoto %llu seq=%u X=%.3f Y=%.3f Z=%.3f\n",
                static_cast<unsigned long long>(pose.id), pose.sequence,
                static_cast<double>(pose.x), static_cast<double>(pose.y),
                static_cast<double>(pose.z));
    }
    void despawn(std::uint64_t id) noexcept override {
        std::fwprintf(stdout, L"[scene] despawn remoto %llu\n",
            static_cast<unsigned long long>(id));
    }
private:
    std::uint64_t updates_ = 0;
};

class ProcessReader final : public MemoryReader {
public:
    explicit ProcessReader(HANDLE process) noexcept : process_(process) {}
    [[nodiscard]] ReadResult read(std::uint32_t address,
                                  std::span<std::byte> destination) noexcept override {
        SIZE_T count = 0;
        const BOOL ok = ReadProcessMemory(process_, reinterpret_cast<LPCVOID>(
            static_cast<std::uintptr_t>(address)), destination.data(), destination.size(), &count);
        return {ok != FALSE, static_cast<std::size_t>(count)};
    }
private:
    HANDLE process_;
};

BOOL WINAPI onConsoleControl(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT) {
        stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

[[nodiscard]] bool parsePid(const wchar_t* text, DWORD& pid) {
    if (!text || !*text || *text == L'-' || *text == L'+') return false;
    wchar_t* end = nullptr;
    errno = 0;
    const unsigned long value = std::wcstoul(text, &end, 10);
    if (*end || errno == ERANGE || value == 0 || value > std::numeric_limits<DWORD>::max()) return false;
    pid = static_cast<DWORD>(value);
    return true;
}

[[nodiscard]] bool findProcesses(std::vector<DWORD>& pids) {
    Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
    if (!snapshot.valid()) return false;
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    if (!Process32FirstW(snapshot.get(), &entry)) return GetLastError() == ERROR_NO_MORE_FILES;
    do {
        if (_wcsicmp(entry.szExeFile, L"gta_sa.exe") == 0) pids.push_back(entry.th32ProcessID);
    } while (Process32NextW(snapshot.get(), &entry));
    return GetLastError() == ERROR_NO_MORE_FILES;
}

[[nodiscard]] bool queryImagePath(HANDLE process, std::wstring& path) {
    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (!QueryFullProcessImageNameW(process, 0, buffer.data(), &length)) return false;
    buffer.resize(length);
    path = std::move(buffer);
    return true;
}

[[nodiscard]] bool checkImageBase(DWORD pid, const std::wstring& imagePath) {
    Handle modules(CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid));
    if (!modules.valid()) return false;
    MODULEENTRY32W module{};
    module.dwSize = sizeof(module);
    if (!Module32FirstW(modules.get(), &module)) return false;
    do {
        if (reinterpret_cast<std::uintptr_t>(module.modBaseAddr) == expectedBase &&
            _wcsicmp(module.szExePath, imagePath.c_str()) == 0) return true;
    } while (Module32NextW(modules.get(), &module));
    return false;
}

[[nodiscard]] bool readAndHashExecutable(const std::wstring& path,
                                         std::vector<std::byte>& bytes,
                                         std::array<std::byte, 32>& digest) {
    Handle file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!file.valid()) return false;
    LARGE_INTEGER length{};
    if (!GetFileSizeEx(file.get(), &length) || length.QuadPart != 14'383'616) return false;
    bytes.resize(static_cast<std::size_t>(length.QuadPart));
    DWORD total = 0;
    while (total < bytes.size()) {
        DWORD read = 0;
        const DWORD remaining = static_cast<DWORD>(bytes.size() - total);
        if (!ReadFile(file.get(), bytes.data() + total, remaining, &read, nullptr) || read == 0)
            return false;
        total += read;
    }
    Algorithm algorithm;
    Hash hash;
    if (!BCRYPT_SUCCESS(BCryptOpenAlgorithmProvider(&algorithm.value, BCRYPT_SHA256_ALGORITHM,
                                                     nullptr, 0)) ||
        !BCRYPT_SUCCESS(BCryptCreateHash(algorithm.value, &hash.value, nullptr, 0, nullptr, 0, 0)) ||
        !BCRYPT_SUCCESS(BCryptHashData(hash.value,
                                      reinterpret_cast<PUCHAR>(bytes.data()),
                                      static_cast<ULONG>(bytes.size()), 0)) ||
        !BCRYPT_SUCCESS(BCryptFinishHash(hash.value,
                                        reinterpret_cast<PUCHAR>(digest.data()),
                                        static_cast<ULONG>(digest.size()), 0))) return false;
    return true;
}

[[nodiscard]] bool approveExecutable(HANDLE process, DWORD pid, std::wstring& path) {
    if (!queryImagePath(process, path) || !checkImageBase(pid, path)) return false;
    std::vector<std::byte> bytes;
    std::array<std::byte, 32> sha256{};
    if (!readAndHashExecutable(path, bytes, sha256)) return false;
    const auto inspection = grandmp::windows::inspectExecutable(bytes);
    return grandmp::windows::recognizeGameVersion(inspection, sha256) ==
           grandmp::windows::GameVersion::gta_sa_10us_hoodlum;
}

[[nodiscard]] bool processAlive(HANDLE process) {
    DWORD exitCode = 0;
    return GetExitCodeProcess(process, &exitCode) != FALSE && exitCode == STILL_ACTIVE;
}

[[nodiscard]] const wchar_t* stateName(ObserverState state) {
    switch (state) {
    case ObserverState::game_not_found: return L"GTA não encontrado";
    case ObserverState::waiting_for_player: return L"GTA identificado; aguardando jogador";
    case ObserverState::player_available: return L"Jogador disponível";
    case ObserverState::temporarily_unavailable: return L"Leitura temporariamente indisponível";
    case ObserverState::process_exited: return L"Processo encerrado";
    }
    return L"Estado desconhecido";
}

[[nodiscard]] const wchar_t* reasonName(PositionStatus status) {
    switch (status) {
    case PositionStatus::available: return L"ok";
    case PositionStatus::player_absent: return L"ped nulo";
    case PositionStatus::invalid_pointer: return L"ponteiro inválido";
    case PositionStatus::read_failed: return L"ReadProcessMemory falhou";
    case PositionStatus::partial_read: return L"leitura parcial";
    case PositionStatus::changed_pointer: return L"ponteiro alterado durante leitura";
    case PositionStatus::non_finite: return L"coordenadas não finitas";
    }
    return L"erro desconhecido";
}

}  // namespace

int wmain(int argc, wchar_t* argv[]) {
    if (argc == 2 && (std::wcscmp(argv[1], L"--help") == 0 ||
                      std::wcscmp(argv[1], L"-h") == 0)) {
        std::fwprintf(stdout, L"Uso: grandmp_position_observer.exe [--pid <PID>] "
                              L"[--connect 127.0.0.1:<porta>] [--remote-scene-preview]\n"
                              L"Aguarda gta_sa.exe Hoodlum e mostra X/Y/Z; Ctrl+C encerra.\n"
                              L"A rede UDP experimental so e ativada com --connect.\n");
        return 0;
    }
    DWORD selectedPid = 0;
    std::uint16_t serverPort = 0;
    bool online = false;
    bool scenePreview = false;
    for (int argument = 1; argument < argc;) {
        if (std::wcscmp(argv[argument], L"--remote-scene-preview") == 0 && !scenePreview) {
            scenePreview = true;
            ++argument;
            continue;
        }
        if (argument + 1 >= argc) {
            std::fwprintf(stderr, L"Falta valor para opcao.\n");
            return 2;
        }
        if (std::wcscmp(argv[argument], L"--pid") == 0 && selectedPid == 0) {
            if (!parsePid(argv[argument + 1], selectedPid)) return 2;
        } else if (std::wcscmp(argv[argument], L"--connect") == 0 && !online) {
            if (!grandmp::windows::parseLoopbackEndpoint(argv[argument + 1], serverPort)) {
                std::fwprintf(stderr, L"--connect exige 127.0.0.1:<porta> (1 a 65535).\n");
                return 2;
            }
            online = true;
        } else {
            std::fwprintf(stderr, L"Uso: grandmp_position_observer.exe [--pid <PID>] "
                                  L"[--connect 127.0.0.1:<porta>] [--remote-scene-preview]\n");
            return 2;
        }
        argument += 2;
    }
    if (scenePreview && !online) {
        std::fwprintf(stderr, L"--remote-scene-preview requer --connect.\n");
        return 2;
    }
    if (!SetConsoleCtrlHandler(onConsoleControl, TRUE)) {
        std::fwprintf(stderr, L"Falha ao instalar tratamento de Ctrl+C (%lu).\n", GetLastError());
        return 2;
    }

    ConsoleScenePreview sceneSink;
    grandmp::windows::ObserverNetwork network;
    if (scenePreview) network.setRemoteSceneSink(&sceneSink);
    if (online && !network.start(serverPort)) {
        std::fwprintf(stderr, L"Nao foi possivel iniciar o socket UDP de loopback.\n");
        return 2;
    }

    PollState state;
    std::fwprintf(stdout, L"GTA não encontrado; procurando gta_sa.exe. Ctrl+C encerra.\n");
    while (!stopRequested.load(std::memory_order_relaxed)) {
        std::vector<DWORD> pids;
        if (!findProcesses(pids)) {
            std::fwprintf(stderr, L"Falha ao enumerar processos (%lu).\n", GetLastError());
            return 2;
        }
        if (selectedPid != 0) {
            pids.erase(std::remove_if(pids.begin(), pids.end(), [selectedPid](DWORD pid) {
                return pid != selectedPid;
            }), pids.end());
        }
        if (pids.empty()) {
            const auto previous = state.state();
            const auto current = state.update(false, PositionStatus::player_absent);
            if (current != previous) {
                std::fwprintf(stdout, L"%ls.\n", stateName(current));
            }
            if (current == ObserverState::process_exited) return 0;
            Sleep(200);
            continue;
        }
        if (pids.size() != 1) {
            std::fwprintf(stderr, L"Múltiplos gta_sa.exe encontrados. Reinicie com --pid <PID>.\n");
            for (const auto pid : pids) std::fwprintf(stderr, L"  PID %lu\n", pid);
            return 2;
        }
        const DWORD pid = pids.front();
        Handle process(OpenProcess(grandmp::windows::observerProcessAccess, FALSE, pid));
        if (!process.valid()) {
            if (GetLastError() == ERROR_INVALID_PARAMETER) {
                std::fwprintf(stdout, L"PID %lu encerrou antes da abertura.\n", pid);
                if (selectedPid != 0) return 0;
                continue;
            }
            std::fwprintf(stderr, L"OpenProcess falhou para PID %lu (%lu).\n", pid, GetLastError());
            return 2;
        }
        std::wstring imagePath;
        if (!approveExecutable(process.get(), pid, imagePath)) {
            if (!processAlive(process.get())) {
                std::fwprintf(stdout, L"PID %lu encerrou durante a validação.\n", pid);
                return 0;
            }
            std::fwprintf(stderr, L"PID %lu recusado: arquivo, PE32/i386, SHA-256 ou base 0x00400000 não homologados.\n", pid);
            return 2;
        }
        std::fwprintf(stdout, L"Hoodlum homologado: PID %lu, %ls\n", pid, imagePath.c_str());
        ProcessReader reader(process.get());
        while (!stopRequested.load(std::memory_order_relaxed)) {
            if (!processAlive(process.get())) {
                (void)state.update(false, PositionStatus::player_absent);
                std::fwprintf(stdout, L"%ls.\n", stateName(state.state()));
                return 0;
            }
            const auto sample = grandmp::windows::readPlayerPosition(reader);
            if (sample.status == PositionStatus::read_failed ||
                sample.status == PositionStatus::partial_read) {
                // STILL_ACTIVE também pode ser o código de saída 259. Numa falha de leitura,
                // confirme a presença do PID para não repetir indefinidamente uma amostra inválida.
                std::vector<DWORD> currentPids;
                if (!findProcesses(currentPids)) {
                    std::fwprintf(stderr, L"Falha ao confirmar o PID %lu (%lu).\n", pid,
                                  GetLastError());
                    return 2;
                }
                if (std::find(currentPids.begin(), currentPids.end(), pid) == currentPids.end()) {
                    (void)state.update(false, sample.status);
                    std::fwprintf(stdout, L"%ls.\n", stateName(state.state()));
                    return 0;
                }
            }
            const auto previous = state.state();
            const auto current = state.update(true, sample.status);
            if (online) {
                const bool connectedBefore = network.connected();
                if (sample.status == PositionStatus::available)
                    network.tick(sample.coordinates);
                else
                    network.tick(std::nullopt);
                if (network.connected() != connectedBefore) {
                    std::fwprintf(stdout, network.connected()
                        ? L"Sessao UDP GrandMP estabelecida.\n"
                        : L"Sessao UDP indisponivel; tentando reconectar.\n");
                }
            }
            if (current == ObserverState::player_available) {
                const Coordinates& c = sample.coordinates;
                std::fwprintf(stdout, L"PID %lu X=%.3f Y=%.3f Z=%.3f\n", pid,
                              static_cast<double>(c.x), static_cast<double>(c.y),
                              static_cast<double>(c.z));
            } else if (current != previous) {
                std::fwprintf(stdout, L"%ls (%ls).\n", stateName(current), reasonName(sample.status));
            }
            Sleep(200);
        }
        return 0;
    }
    std::fwprintf(stdout, L"Encerrado por Ctrl+C.\n");
    return 0;
}
