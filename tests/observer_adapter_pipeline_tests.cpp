#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "grandmp/core/remote_player_adapter.hpp"
#include "grandmp/core/protocol.hpp"
#include "grandmp/windows/observer_network.hpp"

#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <variant>
#include <vector>

namespace {
using grandmp::core::IRemotePlayerEntityBackend;
using grandmp::core::QueuedRemotePlayerAdapter;
using grandmp::core::RemotePose;
using grandmp::protocol::Message;

class WinsockRuntime final {
public:
    WinsockRuntime() noexcept {
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    }
    ~WinsockRuntime() { if (ready_) WSACleanup(); }
    [[nodiscard]] bool ready() const noexcept { return ready_; }
private:
    bool ready_ = false;
};

class Socket final {
public:
    explicit Socket(SOCKET value) noexcept : value_(value) {}
    ~Socket() { if (value_ != INVALID_SOCKET) closesocket(value_); }
    [[nodiscard]] SOCKET get() const noexcept { return value_; }
private:
    SOCKET value_;
};

class RecordingBackend final : public IRemotePlayerEntityBackend {
public:
    bool networkPhase = false;
    std::thread::id owner{};
    bool affinityViolation = false;
    std::map<std::uint64_t, RemotePose> entities;
    std::map<std::uint64_t, unsigned> createAttempts;
    std::vector<std::string> events;

    bool available() const noexcept override { return !networkPhase; }
    bool create(const RemotePose& pose) noexcept override {
        if (networkPhase || owner != std::this_thread::get_id()) {
            affinityViolation = true;
            return false;
        }
        auto& attempts = createAttempts[pose.id];
        ++attempts;
        if (pose.id == 902 && attempts == 1) return false;
        entities[pose.id] = pose;
        events.emplace_back("create");
        return true;
    }
    bool update(const RemotePose& pose) noexcept override {
        if (networkPhase || owner != std::this_thread::get_id()) {
            affinityViolation = true;
            return false;
        }
        const auto found = entities.find(pose.id);
        if (found == entities.end()) return false;
        found->second = pose;
        events.emplace_back("update");
        return true;
    }
    bool remove(std::uint64_t id) noexcept override {
        if (networkPhase || owner != std::this_thread::get_id()) {
            affinityViolation = true;
            return false;
        }
        entities.erase(id);
        events.emplace_back("remove");
        return true;
    }
    bool removeAll() noexcept override {
        if (networkPhase || owner != std::this_thread::get_id()) {
            affinityViolation = true;
            return false;
        }
        entities.clear();
        events.emplace_back("remove_all");
        return true;
    }
};

sockaddr_in loopback(std::uint16_t port) noexcept {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

bool sendMessage(SOCKET socket, const sockaddr_in& peer, const Message& message) {
    const auto bytes = grandmp::protocol::encode(message);
    return bytes && sendto(socket, reinterpret_cast<const char*>(bytes->data()),
        static_cast<int>(bytes->size()), 0, reinterpret_cast<const sockaddr*>(&peer),
        sizeof(peer)) == static_cast<int>(bytes->size());
}

std::optional<Message> receiveMessage(SOCKET socket, sockaddr_in& peer, int timeoutMs) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select(0, &readable, nullptr, nullptr, &timeout) != 1) return std::nullopt;
    std::array<std::uint8_t, grandmp::protocol::kMaxPacketSize + 1> bytes{};
    int peerSize = sizeof(peer);
    const int count = recvfrom(socket, reinterpret_cast<char*>(bytes.data()),
        static_cast<int>(bytes.size()), 0, reinterpret_cast<sockaddr*>(&peer), &peerSize);
    if (count < 0 || peerSize != sizeof(peer)) return std::nullopt;
    return grandmp::protocol::decode(std::span<const std::uint8_t>(
        bytes.data(), static_cast<std::size_t>(count))).message;
}

bool run() {
    WinsockRuntime winsock;
    if (!winsock.ready()) return false;
    bool result = false;
    {
        Socket server(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (server.get() == INVALID_SOCKET) return false;
        auto serverAddress = loopback(0);
        if (bind(server.get(), reinterpret_cast<const sockaddr*>(&serverAddress),
                 sizeof(serverAddress)) == SOCKET_ERROR) return false;
        int addressSize = sizeof(serverAddress);
        if (getsockname(server.get(), reinterpret_cast<sockaddr*>(&serverAddress),
                        &addressSize) == SOCKET_ERROR) return false;

        RecordingBackend backend;
        QueuedRemotePlayerAdapter adapter(backend);
        grandmp::windows::ObserverNetwork network;
        backend.owner = std::this_thread::get_id();
        if (!adapter.initialize() || !adapter.bindOwnerThread()) return false;
        network.setRemoteSceneSink(&adapter);
        if (!network.start(ntohs(serverAddress.sin_port)) || !network.beginRemoteScenePreview()) {
            return false;
        }

        auto step = [&] {
            backend.networkPhase = true;
            network.tick(std::nullopt);
            backend.networkPhase = false;
            (void)adapter.pump(64);
        };
        result = [&]() {
            step();
            sockaddr_in client{};
            const auto hello = receiveMessage(server.get(), client, 1000);
            const auto* helloValue = hello ? std::get_if<grandmp::protocol::Hello>(&*hello) : nullptr;
            if (!helloValue) return false;
            const auto nonce = helloValue->nonce;
            if (!sendMessage(server.get(), client, grandmp::protocol::Welcome{nonce, 5000}))
                return false;
            for (int attempt = 0; attempt < 50 && !network.connected(); ++attempt) {
                step();
                Sleep(2);
            }
            if (network.sessionId() != 5000) return false;

            if (!sendMessage(server.get(), client, grandmp::protocol::RemoteJoin{900}) ||
                !sendMessage(server.get(), client, grandmp::protocol::RemotePosition{900, 1, 8, 9, 10}) ||
                !sendMessage(server.get(), client, grandmp::protocol::RemoteJoin{901}) ||
                !sendMessage(server.get(), client, grandmp::protocol::RemotePosition{901, 1, 18, 19, 20}))
                return false;
            for (int attempt = 0; attempt < 100 && backend.entities.size() < 2; ++attempt) {
                step();
                Sleep(2);
            }
            if (backend.entities.size() != 2 || !backend.entities.contains(900) ||
                !backend.entities.contains(901)) return false;

            // Duplicate/out-of-order sequence 1 cannot replace the newer target.
            if (!sendMessage(server.get(), client,
                    grandmp::protocol::RemotePosition{900, 1, 999, 999, 999}) ||
                !sendMessage(server.get(), client,
                    grandmp::protocol::RemotePosition{900, 2, 15, 9, 10}) ||
                !sendMessage(server.get(), client,
                    grandmp::protocol::RemotePosition{901, 1, 999, 999, 999})) return false;
            const ULONGLONG poseDeadline = GetTickCount64() + 500;
            while (GetTickCount64() < poseDeadline) { step(); Sleep(2); }
            if (backend.entities.at(900).sequence != 2 || backend.entities.at(900).x != 15 ||
                backend.entities.at(901).x != 18) return false;

            if (!sendMessage(server.get(), client, grandmp::protocol::RemoteLeave{900})) return false;
            for (int attempt = 0; attempt < 100 && backend.entities.contains(900); ++attempt) {
                step();
                Sleep(2);
            }
            if (backend.entities.contains(900) || !backend.entities.contains(901)) return false;

            // A replacement Welcome is a server restart boundary; ID 900 may be reused.
            // A server snapshot can follow Welcome in the same receive batch.
            if (!sendMessage(server.get(), client,
                    grandmp::protocol::Welcome{nonce, 6000}) ||
                !sendMessage(server.get(), client,
                    grandmp::protocol::RemotePosition{900, 1, 100, 2, 3})) return false;
            step();
            if (network.sessionId() != 6000) return false;
            for (int attempt = 0; attempt < 100 && !backend.entities.contains(900); ++attempt) {
                step();
                Sleep(2);
            }
            if (backend.entities.size() != 1 || !backend.entities.contains(900) ||
                backend.entities.at(900).x != 100) return false;

            if (!sendMessage(server.get(), client,
                    grandmp::protocol::RemotePosition{902, 1, 40, 5, 6})) return false;
            for (int attempt = 0; attempt < 100 && !backend.entities.contains(902); ++attempt) {
                step();
                Sleep(2);
            }
            if (!backend.entities.contains(902) || backend.createAttempts[902] < 2) return false;

            // Producers racing shutdown cannot call the backend or revive work.
            std::thread producer([&] {
                for (std::uint64_t id = 1000; id < 1100; ++id)
                    adapter.spawn(RemotePose{id, 1, 0, 0, 0});
            });
            adapter.shutdown();
            producer.join();
            network.stop();
            (void)adapter.pump();
            const bool clean = adapter.state() == grandmp::core::AdapterState::stopped &&
                               backend.entities.empty() && !backend.affinityViolation;
            if (!clean) std::fprintf(stderr, "observer adapter pipeline cleanup/affinity failed\n");
            else std::puts("observer -> roster -> scene -> adapter -> simulated backend PASS");
            return clean;
        }();

        if (adapter.state() != grandmp::core::AdapterState::stopped) {
            network.stop();
            adapter.shutdown();
            (void)adapter.pump();
        }
    }
    return result;
}
}  // namespace

int main() { return run() ? 0 : 1; }
