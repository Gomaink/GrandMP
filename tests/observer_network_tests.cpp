#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include "grandmp/core/protocol.hpp"
#include "grandmp/windows/observer_network.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <span>
#include <variant>

namespace {

struct RecordingSceneSink final : grandmp::core::IRemotePlayerSceneSink {
    int spawned = 0;
    int moved = 0;
    int removed = 0;
    std::uint64_t lastId = 0;
    void spawn(const grandmp::core::RemotePose& pose) noexcept override {
        ++spawned; lastId = pose.id;
    }
    void move(const grandmp::core::RemotePose& pose) noexcept override {
        ++moved; lastId = pose.id;
    }
    void despawn(std::uint64_t id) noexcept override { ++removed; lastId = id; }
};

class SocketGuard {
public:
    explicit SocketGuard(SOCKET value) : value_(value) {}
    ~SocketGuard() { if (value_ != INVALID_SOCKET) closesocket(value_); }
    [[nodiscard]] SOCKET get() const { return value_; }
private:
    SOCKET value_;
};

[[nodiscard]] sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

[[nodiscard]] std::optional<grandmp::protocol::Message> receive(
    SOCKET socket, sockaddr_in& sender, int timeoutMs) {
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(socket, &readable);
    timeval timeout{timeoutMs / 1000, (timeoutMs % 1000) * 1000};
    if (select(0, &readable, nullptr, nullptr, &timeout) != 1) return std::nullopt;
    std::array<std::uint8_t, grandmp::protocol::kMaxPacketSize + 1> bytes{};
    int senderSize = sizeof(sender);
    const int count = recvfrom(socket, reinterpret_cast<char*>(bytes.data()),
                               static_cast<int>(bytes.size()), 0,
                               reinterpret_cast<sockaddr*>(&sender), &senderSize);
    if (count < 0 || senderSize != sizeof(sender)) return std::nullopt;
    const auto parsed = grandmp::protocol::decode(std::span<const std::uint8_t>(
        bytes.data(), static_cast<std::size_t>(count)));
    return parsed.message;
}

// A fila UDP pode conter mensagens auxiliares (por exemplo SnapshotRequest)
// e posições anteriores. Não presumir que o próximo datagrama terá um tipo
// específico: procurar a mensagem esperada dentro de um prazo limitado.
template <typename Predicate>
[[nodiscard]] bool waitForPacket(SOCKET socket, int timeoutMs, Predicate&& matches) {
    const ULONGLONG deadline = GetTickCount64() + static_cast<ULONGLONG>(timeoutMs);
    do {
        sockaddr_in sender{};
        const auto packet = receive(socket, sender, 20);
        if (packet && matches(*packet, sender)) return true;
    } while (GetTickCount64() < deadline);
    return false;
}

[[nodiscard]] bool sendMessage(SOCKET socket, const sockaddr_in& recipient,
                               const grandmp::protocol::Message& message) {
    const auto bytes = grandmp::protocol::encode(message);
    if (!bytes) return false;
    return sendto(socket, reinterpret_cast<const char*>(bytes->data()),
                  static_cast<int>(bytes->size()), 0,
                  reinterpret_cast<const sockaddr*>(&recipient), sizeof(recipient)) ==
           static_cast<int>(bytes->size());
}

}  // namespace

int main() {
    std::uint16_t port = 0;
    if (!grandmp::windows::parseLoopbackEndpoint(L"127.0.0.1:7777", port) || port != 7777 ||
        grandmp::windows::parseLoopbackEndpoint(L"localhost:7777", port) ||
        grandmp::windows::parseLoopbackEndpoint(L"192.168.1.1:7777", port) ||
        grandmp::windows::parseLoopbackEndpoint(L"127.0.0.1:0", port) ||
        grandmp::windows::parseLoopbackEndpoint(L"127.0.0.1:65536", port)) return 1;

    // Sem start (equivalente ao modo sem --connect), tick não abre socket.
    grandmp::windows::ObserverNetwork offline;
    offline.tick(grandmp::windows::Coordinates{1, 2, 3});
    if (offline.connected()) return 2;

    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return 3;
    int result = 0;
    {
        SocketGuard server(socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
        if (server.get() == INVALID_SOCKET) result = 4;
        sockaddr_in address = loopback(0);
        if (!result && bind(server.get(), reinterpret_cast<sockaddr*>(&address),
                            sizeof(address)) == SOCKET_ERROR) result = 5;
        int addressSize = sizeof(address);
        if (!result && getsockname(server.get(), reinterpret_cast<sockaddr*>(&address),
                                   &addressSize) == SOCKET_ERROR) result = 6;
        RecordingSceneSink sceneSink;
        grandmp::windows::ObserverNetwork observer;
        if (!result && !observer.start(ntohs(address.sin_port))) result = 7;
        if (!result) {
            observer.tick(std::nullopt);
            observer.tick(grandmp::windows::Coordinates{
                std::numeric_limits<float>::quiet_NaN(), 2, 3});
            sockaddr_in sender{};
            if (receive(server.get(), sender, 0)) result = 8;
        }
        sockaddr_in client{};
        std::uint64_t nonce = 0;
        if (!result) {
            observer.tick(grandmp::windows::Coordinates{10, 20, 30});
            auto hello = receive(server.get(), client, 1000);
            const auto* value = hello ? std::get_if<grandmp::protocol::Hello>(&*hello) : nullptr;
            if (!value || value->nonce == 0) result = 9;
            else nonce = value->nonce;
        }
        if (!result && !sendMessage(server.get(), client,
                                    grandmp::protocol::Welcome{nonce, 1234})) result = 10;
        if (!result) {
            // UDP loopback pode entregar após tick; limite de 1 s, sem depender
            // de uma frequência específica de agendamento.
            const ULONGLONG deadline = GetTickCount64() + 1000;
            bool receivedPosition = false;
            while (GetTickCount64() < deadline && !receivedPosition) {
                observer.tick(grandmp::windows::Coordinates{10, 20, 30});
                sockaddr_in sender{};
                auto packet = receive(server.get(), sender, 20);
                const auto* position = packet
                    ? std::get_if<grandmp::protocol::PlayerPosition>(&*packet) : nullptr;
                if (position && position->session_id == 1234 && position->sequence >= 1 &&
                    position->x == 10 && position->y == 20 && position->z == 30)
                    receivedPosition = true;
            }
            if (!receivedPosition || !observer.connected()) result = 11;
        }
        if (!result) {
            // Um Welcome com novo identificador representa reinício do servidor.
            // A primeira posição da nova sessão precisa recomeçar em sequência 1.
            if (!sendMessage(server.get(), client,
                             grandmp::protocol::Welcome{nonce, 5678})) result = 12;
        }
        if (!result) {
            const ULONGLONG deadline = GetTickCount64() + 1000;
            bool reconnected = false;
            while (GetTickCount64() < deadline && !reconnected) {
                observer.tick(grandmp::windows::Coordinates{11, 21, 31});
                sockaddr_in sender{};
                auto packet = receive(server.get(), sender, 20);
                const auto* position = packet
                    ? std::get_if<grandmp::protocol::PlayerPosition>(&*packet) : nullptr;
                if (position && position->session_id == 5678 && position->sequence == 1 &&
                    position->x == 11 && position->y == 21 && position->z == 31)
                    reconnected = true;
            }
            if (!reconnected) result = 13;
        }
        if (!result) {
            observer.setRemoteSceneSink(&sceneSink);
            if (!sendMessage(server.get(), client, grandmp::protocol::RemoteJoin{900}) ||
                !sendMessage(server.get(), client,
                             grandmp::protocol::RemotePosition{900, 1, 8, 9, 10})) result = 17;
        }
        if (!result) {
            const ULONGLONG deadline = GetTickCount64() + 1000;
            while (GetTickCount64() < deadline && sceneSink.spawned == 0) {
                observer.tick(grandmp::windows::Coordinates{11, 21, 31});
                Sleep(1);
            }
            if (sceneSink.spawned != 1 || sceneSink.lastId != 900) result = 18;
        }
        if (!result && !sendMessage(server.get(), client,
                                    grandmp::protocol::RemoteLeave{900})) result = 19;
        if (!result) {
            const ULONGLONG deadline = GetTickCount64() + 1000;
            while (GetTickCount64() < deadline && sceneSink.removed == 0) {
                observer.tick(grandmp::windows::Coordinates{11, 21, 31});
                Sleep(1);
            }
            if (sceneSink.removed != 1 || sceneSink.lastId != 900) result = 20;
        }
        if (!result) {
            observer.stop();
            const bool disconnected = waitForPacket(server.get(), 1000,
                [](const grandmp::protocol::Message& message, const sockaddr_in&) {
                    const auto* value = std::get_if<grandmp::protocol::Disconnect>(&message);
                    return value && value->session_id == 5678;
                });
            if (!disconnected || observer.connected()) result = 14;
        }
        if (!result) {
            // Esvaziar datagramas enviados *antes* de stop() para que o teste
            // de tick após stop não confunda um pacote antigo com envio novo.
            sockaddr_in sender{};
            int drained = 0;
            while (drained < 128 && receive(server.get(), sender, 20)) ++drained;
            if (drained == 128) result = 16;
            if (!result) {
                observer.tick(grandmp::windows::Coordinates{1, 2, 3});
                if (receive(server.get(), sender, 100)) result = 15;
            }
        }
    }
    WSACleanup();
    if (result != 0) {
        std::fprintf(stderr, "observer_network_loopback: falha no passo %d\n", result);
    }
    return result;
}
