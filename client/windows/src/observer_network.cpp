#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>

#include "grandmp/windows/observer_network.hpp"
#include "grandmp/core/protocol.hpp"

#include <array>
#include <cstdint>
#include <cwchar>
#include <limits>
#include <exception>
#include <new>
#include <span>
#include <variant>

namespace grandmp::windows {
namespace {

constexpr std::uint64_t helloIntervalMs = 3000;
constexpr std::uint64_t welcomeTimeoutMs = 10000;
constexpr std::uint64_t snapshotIntervalMs = 3000;

[[nodiscard]] SOCKET nativeSocket(std::uintptr_t value) noexcept {
    return static_cast<SOCKET>(value);
}

[[nodiscard]] sockaddr_in serverAddress(std::uint16_t port) noexcept {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    return address;
}

[[nodiscard]] bool sendPacket(SOCKET socket, std::uint16_t port,
                              const protocol::Message& message) noexcept {
    try {
        const auto packet = protocol::encode(message);
        if (!packet) return false;
        const sockaddr_in address = serverAddress(port);
        const int sent = sendto(socket, reinterpret_cast<const char*>(packet->data()),
                                static_cast<int>(packet->size()), 0,
                                reinterpret_cast<const sockaddr*>(&address), sizeof(address));
        return sent == static_cast<int>(packet->size());
    } catch (const std::bad_alloc&) {
        return false;
    }
}

}  // namespace

bool parseLoopbackEndpoint(std::wstring_view address, std::uint16_t& port) noexcept {
    constexpr std::wstring_view prefix = L"127.0.0.1:";
    if (!address.starts_with(prefix) || address.size() == prefix.size()) return false;
    std::uint32_t value = 0;
    for (const wchar_t digit : address.substr(prefix.size())) {
        if (digit < L'0' || digit > L'9') return false;
        value = value * 10 + static_cast<std::uint32_t>(digit - L'0');
        if (value > std::numeric_limits<std::uint16_t>::max()) return false;
    }
    if (value == 0) return false;
    port = static_cast<std::uint16_t>(value);
    return true;
}

ObserverNetwork::~ObserverNetwork() { stop(); }

void ObserverNetwork::resetRemoteScene() noexcept {
    if (sceneSink_) {
        scene_.reset(*sceneSink_);
        // The boundary must precede new-session spawn/poses even when Welcome
        // and its snapshot arrive in the same receivePackets() batch.
        sceneSink_->resetSession();
    }
}

void ObserverNetwork::setRemoteSceneSink(core::IRemotePlayerSceneSink* sink) noexcept {
    resetRemoteScene();
    sceneSink_ = sink;
}


bool ObserverNetwork::start(std::uint16_t port) noexcept {
    if (winsockStarted_ || port == 0) return false;
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
    winsockStarted_ = true;
    const SOCKET socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket == INVALID_SOCKET) { stop(); return false; }
    socket_ = static_cast<std::uintptr_t>(socket);
    u_long nonblocking = 1;
    if (ioctlsocket(socket, FIONBIO, &nonblocking) == SOCKET_ERROR ||
        !BCRYPT_SUCCESS(BCryptGenRandom(nullptr, reinterpret_cast<PUCHAR>(&nonce_),
                                        sizeof(nonce_), BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        stop();
        return false;
    }
    if (nonce_ == 0) nonce_ = 1;
    port_ = port;
    return true;
}

bool ObserverNetwork::beginRemoteScenePreview() noexcept {
    if (!winsockStarted_ || socket_ == static_cast<std::uintptr_t>(INVALID_SOCKET)) return false;
    initiated_ = true;
    return true;
}

void ObserverNetwork::sendHello() noexcept {
    (void)sendPacket(nativeSocket(socket_), port_, protocol::Hello{nonce_});
    lastHelloMs_ = GetTickCount64();  // Limita retries também se não há servidor.
}

void ObserverNetwork::receivePackets() noexcept {
    std::array<std::uint8_t, protocol::kMaxPacketSize + 1> bytes{};
    // Limite por tick: tráfego espúrio não pode bloquear o polling do jogador.
    for (int attempt = 0; attempt < 128; ++attempt) {
        sockaddr_in sender{};
        int senderSize = sizeof(sender);
        const int received = recvfrom(nativeSocket(socket_),
                                      reinterpret_cast<char*>(bytes.data()),
                                      static_cast<int>(bytes.size()), 0,
                                      reinterpret_cast<sockaddr*>(&sender), &senderSize);
        if (received == SOCKET_ERROR) return;
        if (senderSize != sizeof(sender) || sender.sin_family != AF_INET ||
            sender.sin_addr.s_addr != htonl(INADDR_LOOPBACK) ||
            sender.sin_port != htons(port_)) continue;
        const auto decoded = protocol::decode(std::span<const std::uint8_t>(
            bytes.data(), static_cast<std::size_t>(received)));
        if (decoded.status != protocol::DecodeStatus::ok || !decoded.message) continue;
        const auto nowMs = GetTickCount64();
        const auto now = core::RemoteRoster::Clock::now();
        if (const auto* welcome = std::get_if<protocol::Welcome>(&*decoded.message)) {
            if (welcome->nonce != nonce_ || welcome->session_id == 0) continue;
            if (welcome->session_id != sessionId_) {
                resetRemoteScene();
                roster_.clear();
                sessionId_ = welcome->session_id;
                sequence_ = 0;
                lastSnapshotMs_ = 0;
            }
            lastWelcomeMs_ = nowMs;
        } else if (sessionId_ != 0) {
            if (const auto* join = std::get_if<protocol::RemoteJoin>(&*decoded.message)) {
                if (join->player_id != sessionId_ && roster_.joined(*join, now))
                    std::fwprintf(stdout, L"Jogador remoto %llu entrou.\n",
                                  static_cast<unsigned long long>(join->player_id));
            } else if (const auto* pos = std::get_if<protocol::RemotePosition>(&*decoded.message)) {
                if (pos->player_id == sessionId_) continue;
                const auto result = roster_.position(*pos, now);
                if (result == core::RemoteRoster::PositionResult::added)
                    std::fwprintf(stdout, L"Jogador remoto %llu entrou.\n",
                                  static_cast<unsigned long long>(pos->player_id));
                if (result == core::RemoteRoster::PositionResult::added ||
                    result == core::RemoteRoster::PositionResult::updated)
                    std::fwprintf(stdout, L"Remoto %llu seq=%u X=%.3f Y=%.3f Z=%.3f\n",
                                  static_cast<unsigned long long>(pos->player_id), pos->sequence,
                                  static_cast<double>(pos->x), static_cast<double>(pos->y),
                                  static_cast<double>(pos->z));
            } else if (const auto* leave = std::get_if<protocol::RemoteLeave>(&*decoded.message)) {
                if (roster_.left(*leave, now))
                    std::fwprintf(stdout, L"Jogador remoto %llu saiu.\n",
                                  static_cast<unsigned long long>(leave->player_id));
            }
        }
    }
}

void ObserverNetwork::sendSnapshotRequest() noexcept {
    if (sessionId_ == 0) return;
    (void)sendPacket(nativeSocket(socket_), port_, protocol::SnapshotRequest{sessionId_});
    lastSnapshotMs_ = GetTickCount64();
}

void ObserverNetwork::sendPosition(const Coordinates& position) noexcept {
    if (!protocol::validCoordinates(position.x, position.y, position.z)) return;
    if (sequence_ == std::numeric_limits<std::uint32_t>::max()) {
        sessionId_ = 0;  // Não voltar a 1 na mesma sessão.
        return;
    }
    const std::uint32_t nextSequence = sequence_ + 1;
    if (sendPacket(nativeSocket(socket_), port_, protocol::PlayerPosition{
            sessionId_, nextSequence, position.x, position.y, position.z})) {
        sequence_ = nextSequence;
    }
}

void ObserverNetwork::tick(std::optional<Coordinates> position) noexcept {
    if (!winsockStarted_ || socket_ == static_cast<std::uintptr_t>(INVALID_SOCKET)) return;
    if (position && !protocol::validCoordinates(position->x, position->y, position->z))
        position.reset();
    if (!initiated_) {
        if (!position) return;
        initiated_ = true;
    }
    const std::uint64_t now = GetTickCount64();
    if (sessionId_ != 0 && now - lastWelcomeMs_ >= welcomeTimeoutMs) {
        sessionId_ = 0;
        resetRemoteScene();
        roster_.clear();
    }
    if (lastHelloMs_ == 0 || now - lastHelloMs_ >= helloIntervalMs) sendHello();
    receivePackets();
    if (sessionId_ != 0) {
        if (lastSnapshotMs_ == 0 || now - lastSnapshotMs_ >= snapshotIntervalMs)
            sendSnapshotRequest();
        if (position) sendPosition(*position);
    }
    for (auto id : roster_.expire(core::RemoteRoster::Clock::now()))
        std::fwprintf(stdout, L"Jogador remoto %llu expirou.\n",
                      static_cast<unsigned long long>(id));
    if (sceneSink_) {
        try {
            scene_.synchronize(roster_, core::RemoteRoster::Clock::now(), *sceneSink_);
        } catch (const std::exception&) {
            // Nao abortar a leitura de GTA por falha no cache de apresentacao.
            resetRemoteScene();
        }
    }
}

void ObserverNetwork::stop() noexcept {
    if (socket_ != static_cast<std::uintptr_t>(INVALID_SOCKET)) {
        if (sessionId_ != 0)
            (void)sendPacket(nativeSocket(socket_), port_, protocol::Disconnect{sessionId_});
        closesocket(nativeSocket(socket_));
        socket_ = static_cast<std::uintptr_t>(INVALID_SOCKET);
    }
    if (winsockStarted_) {
        WSACleanup();
        winsockStarted_ = false;
    }
    sessionId_ = 0;
    resetRemoteScene();
    roster_.clear();
    sceneSink_ = nullptr;
    initiated_ = false;
}

}  // namespace grandmp::windows
