#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <windows.h>

#include "grandmp/core/remote_player_adapter.hpp"
#include "grandmp/windows/observer_network.hpp"

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string_view>
#include <thread>

namespace {

std::atomic_bool stopRequested{false};
std::atomic_bool receivingNetwork{false};

BOOL WINAPI onConsoleControl(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT) {
        stopRequested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}

bool parseNumber(std::wstring_view text, unsigned long& value) noexcept {
    if (text.empty()) return false;
    value = 0;
    for (const wchar_t character : text) {
        if (character < L'0' || character > L'9') return false;
        value = value * 10 + static_cast<unsigned long>(character - L'0');
        if (value > 65535) return false;
    }
    return value != 0;
}

class SimulatedEntityBackend final : public grandmp::core::IRemotePlayerEntityBackend {
public:
    void bindOwner() noexcept { owner_ = std::this_thread::get_id(); }
    [[nodiscard]] bool affinityOk() const noexcept { return affinityOk_; }
    [[nodiscard]] const std::map<std::uint64_t, grandmp::core::RemotePose>& entities() const noexcept {
        return entities_;
    }

    [[nodiscard]] bool available() const noexcept override {
        return !receivingNetwork.load(std::memory_order_acquire);
    }
    [[nodiscard]] bool create(const grandmp::core::RemotePose& pose) noexcept override {
        if (!checkContext()) return false;
        try {
            const auto [entity, inserted] = entities_.emplace(pose.id, pose);
            if (!inserted) {
                entity->second = pose;
                return true;
            }
            std::fwprintf(stdout, L"[backend] spawn id=%llu X=%.3f Y=%.3f Z=%.3f\n",
                static_cast<unsigned long long>(pose.id), static_cast<double>(pose.x),
                static_cast<double>(pose.y), static_cast<double>(pose.z));
            return true;
        } catch (...) { return false; }
    }
    [[nodiscard]] bool update(const grandmp::core::RemotePose& pose) noexcept override {
        if (!checkContext()) return false;
        const auto found = entities_.find(pose.id);
        if (found == entities_.end()) return false;
        found->second = pose;
        if (++loggedUpdates_ % 10 == 0)
            std::fwprintf(stdout, L"[backend] pose id=%llu seq=%u X=%.3f Y=%.3f Z=%.3f\n",
                static_cast<unsigned long long>(pose.id), pose.sequence,
                static_cast<double>(pose.x), static_cast<double>(pose.y),
                static_cast<double>(pose.z));
        return true;
    }
    [[nodiscard]] bool remove(std::uint64_t id) noexcept override {
        if (!checkContext()) return false;
        entities_.erase(id);
        std::fwprintf(stdout, L"[backend] despawn id=%llu\n", static_cast<unsigned long long>(id));
        return true;
    }
    [[nodiscard]] bool removeAll() noexcept override {
        if (!checkContext()) return false;
        entities_.clear();
        std::fwprintf(stdout, L"[backend] cleanup all\n");
        return true;
    }

private:
    [[nodiscard]] bool checkContext() noexcept {
        affinityOk_ = affinityOk_ && owner_ == std::this_thread::get_id() &&
                      !receivingNetwork.load(std::memory_order_acquire);
        return affinityOk_;
    }

    std::thread::id owner_{};
    std::map<std::uint64_t, grandmp::core::RemotePose> entities_;
    std::uint64_t loggedUpdates_ = 0;
    bool affinityOk_ = true;
};

void printUsage() {
    std::fwprintf(stdout,
        L"Uso: grandmp_remote_scene_host.exe [--port 7777] [--duration-ms N]\n"
        L"Recebe cena UDP em loopback e a aplica somente ao backend simulado. Ctrl+C encerra.\n");
}

}  // namespace

int wmain(int argc, wchar_t** argv) {
    std::uint16_t port = 7777;
    unsigned long durationMs = 0;
    for (int index = 1; index < argc; ++index) {
        const std::wstring_view option{argv[index]};
        if (option == L"--help") { printUsage(); return 0; }
        if ((option == L"--port" || option == L"--duration-ms") && index + 1 < argc) {
            unsigned long number = 0;
            if (!parseNumber(argv[++index], number)) return 2;
            if (option == L"--port") port = static_cast<std::uint16_t>(number);
            else durationMs = number;
            continue;
        }
        printUsage();
        return 2;
    }
    if (!SetConsoleCtrlHandler(onConsoleControl, TRUE)) return 2;

    // Destruction order keeps the sink and backend alive through network.stop().
    SimulatedEntityBackend backend;
    grandmp::core::QueuedRemotePlayerAdapter adapter(backend);
    grandmp::windows::ObserverNetwork network;
    backend.bindOwner();
    if (!adapter.initialize() || !adapter.bindOwnerThread()) return 2;
    network.setRemoteSceneSink(&adapter);
    if (!network.start(port) || !network.beginRemoteScenePreview()) {
        std::fwprintf(stderr, L"Falha ao iniciar preview UDP em 127.0.0.1:%u.\n", port);
        network.stop();
        adapter.shutdown();
        (void)adapter.pump();
        return 2;
    }

    std::fwprintf(stdout, L"Preview remoto em 127.0.0.1:%u; nenhum GTA é iniciado.\n", port);
    const auto started = GetTickCount64();
    std::uint64_t previousSession = 0;
    while (!stopRequested.load(std::memory_order_relaxed) &&
           (durationMs == 0 || GetTickCount64() - started < durationMs)) {
        receivingNetwork.store(true, std::memory_order_release);
        network.tick(std::nullopt);
        receivingNetwork.store(false, std::memory_order_release);

        const std::uint64_t session = network.sessionId();
        if (session != previousSession) {
            if (previousSession != 0) {
                std::fwprintf(stdout, L"[host] sessao remota mudou; boundary aplicado pelo sink.\n");
            }
            previousSession = session;
        }
        (void)adapter.pump(64);
        Sleep(20);
    }

    receivingNetwork.store(true, std::memory_order_release);
    network.stop();
    receivingNetwork.store(false, std::memory_order_release);
    adapter.shutdown();
    (void)adapter.pump();
    const bool clean = adapter.state() == grandmp::core::AdapterState::stopped &&
                       backend.entities().empty() && backend.affinityOk();
    std::fwprintf(stdout, L"Preview encerrado: cleanup=%ls affinity=%ls\n",
                  clean ? L"ok" : L"falhou", backend.affinityOk() ? L"ok" : L"falhou");
    return clean ? 0 : 3;
}
