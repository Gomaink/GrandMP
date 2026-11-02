#include "grandmp/server/sessions.hpp"
#include "grandmp/core/ipv4_endpoint.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <atomic>
#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <span>
#include <string>
#include <string_view>

namespace {

#ifdef _WIN32
using NativeSocket = SOCKET;
constexpr NativeSocket invalidSocket = INVALID_SOCKET;
#else
using NativeSocket = int;
constexpr NativeSocket invalidSocket = -1;
#endif

#ifdef _WIN32
std::atomic_bool stopRequested{false};
#else
volatile std::sig_atomic_t stopRequested = 0;
#endif

#ifdef _WIN32
BOOL WINAPI onConsoleControl(DWORD control) {
    if (control == CTRL_C_EVENT || control == CTRL_BREAK_EVENT || control == CTRL_CLOSE_EVENT) {
        stopRequested = 1;
        return TRUE;
    }
    return FALSE;
}
#else
void onSignal(int) { stopRequested = 1; }
#endif

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        valid_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        valid_ = true;
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        if (valid_) WSACleanup();
#endif
    }
    [[nodiscard]] bool valid() const noexcept { return valid_; }
private:
    bool valid_ = false;
};

class Socket {
public:
    explicit Socket(NativeSocket value = invalidSocket) noexcept : value_(value) {}
    ~Socket() {
        if (value_ != invalidSocket) {
#ifdef _WIN32
            closesocket(value_);
#else
            close(value_);
#endif
        }
    }
    Socket(const Socket&) = delete;
    Socket& operator=(const Socket&) = delete;
    [[nodiscard]] bool valid() const noexcept { return value_ != invalidSocket; }
    [[nodiscard]] NativeSocket get() const noexcept { return value_; }
private:
    NativeSocket value_;
};

[[nodiscard]] bool parsePort(std::string_view text, std::uint16_t& port) {
    unsigned value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() ||
        value == 0 || value > std::numeric_limits<std::uint16_t>::max()) return false;
    port = static_cast<std::uint16_t>(value);
    return true;
}

[[nodiscard]] grandmp::server::Endpoint endpointFrom(const sockaddr_in& address) {
    const auto ip = ntohl(address.sin_addr.s_addr);
    return {{{static_cast<std::uint8_t>(ip >> 24U),
              static_cast<std::uint8_t>(ip >> 16U),
              static_cast<std::uint8_t>(ip >> 8U),
              static_cast<std::uint8_t>(ip)}}, ntohs(address.sin_port)};
}

[[nodiscard]] sockaddr_in toSockaddr(const grandmp::server::Endpoint& endpoint) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    const auto& b = endpoint.address;
    const std::uint32_t ip = (std::uint32_t{b[0]} << 24) |
        (std::uint32_t{b[1]} << 16) | (std::uint32_t{b[2]} << 8) | b[3];
    address.sin_addr.s_addr = htonl(ip);
    address.sin_port = htons(endpoint.port);
    return address;
}

[[nodiscard]] bool sendMessage(NativeSocket socket, const grandmp::server::Endpoint& endpoint,
                               const grandmp::protocol::Message& message) {
    const auto packet = grandmp::protocol::encode(message);
    if (!packet) return false;
    const auto address = toSockaddr(endpoint);
    const int sent = sendto(socket, reinterpret_cast<const char*>(packet->data()),
                            static_cast<int>(packet->size()), 0,
                            reinterpret_cast<const sockaddr*>(&address), sizeof(address));
    return sent == static_cast<int>(packet->size());
}

void broadcast(NativeSocket socket, const grandmp::server::SessionTable& sessions,
               std::uint64_t excludedId, const grandmp::protocol::Message& message) {
    for (const auto& peer : sessions.peersExcept(excludedId))
        (void)sendMessage(socket, peer.endpoint, message);
}

// Snapshot idempotente: RemoteJoin + ultima posicao para cada participante.
// O cliente solicita novamente periodicamente para recuperar eventos UDP perdidos.
void sendSnapshot(NativeSocket socket, const grandmp::server::SessionTable& sessions,
                  std::uint64_t clientId, const grandmp::server::Endpoint& endpoint) {
    for (const auto& peer : sessions.peersExcept(clientId)) {
        (void)sendMessage(socket, endpoint, grandmp::protocol::RemoteJoin{peer.id});
        if (peer.position) {
            const auto& pos = *peer.position;
            (void)sendMessage(socket, endpoint, grandmp::protocol::RemotePosition{
                peer.id, pos.sequence, pos.x, pos.y, pos.z});
        }
        if (sessions.onFootSubscribed(clientId)) {
            if (const auto foot=sessions.recentOnFoot(peer.id,grandmp::server::SessionTable::Clock::now()))
                (void)sendMessage(socket,endpoint,grandmp::protocol::RemoteOnFoot{
                    clientId,peer.id,foot->sequence,foot->state});
        }
    }
}

[[nodiscard]] std::uint64_t firstSessionId() {
    std::random_device source;
    const auto candidate = (static_cast<std::uint64_t>(source()) << 32U) | source();
    return 1 + candidate % (std::numeric_limits<std::uint64_t>::max() -
                            grandmp::server::SessionTable::maxSessions);
}

}  // namespace

int main(int argc, char* argv[]) {
    std::uint16_t port = 7777;
    grandmp::core::Ipv4Address bindAddress=grandmp::core::loopbackAddress;
    std::string bindText="127.0.0.1";
    bool bindSet=false,portSet=false;
    constexpr auto usage="Uso: grandmp_server [--port 1..65535] [--bind private-IPv4]\n";
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::cout << usage;
        return 0;
    }
    for(int i=1;i<argc;i+=2) {
        if(i+1>=argc) { std::cerr<<usage;return 2; }
        const std::string_view option=argv[i];
        if(option=="--port" && !portSet && parsePort(argv[i+1],port)) portSet=true;
        else if(option=="--bind" && !bindSet && grandmp::core::parseIpv4Address(argv[i+1],bindAddress) &&
                grandmp::core::validLabEndpoint({bindAddress,port})) {
            bindText=argv[i+1];bindSet=true;
        } else { std::cerr<<usage;return 2; }
    }
    SocketRuntime runtime;
    if (!runtime.valid()) {
        std::cerr << "Falha ao inicializar sockets.\n";
        return 2;
    }
    Socket socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.valid()) {
        std::cerr << "Falha ao criar socket UDP.\n";
        return 2;
    }
    sockaddr_in local{};
    local.sin_family = AF_INET;
    const grandmp::server::Endpoint bindEndpoint{bindAddress,port};
    local.sin_addr.s_addr = toSockaddr(bindEndpoint).sin_addr.s_addr;
    local.sin_port = htons(port);
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&local), sizeof(local)) != 0) {
        std::cerr << "Falha ao escutar " << bindText << ':' << port << ".\n";
        return 2;
    }
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(onConsoleControl, TRUE)) return 2;
#else
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#endif

    grandmp::server::SessionTable sessions(firstSessionId());
    std::array<std::uint8_t, 2048> packet{};
    auto lastRejectLog = grandmp::server::SessionTable::Clock::time_point{};
    std::cout << "GrandMP Server UDP v1 escutando em " << bindText << ':' << port
              << "; Ctrl+C encerra." << std::endl;
    while (!stopRequested) {
        fd_set readSet;
        FD_ZERO(&readSet);
        FD_SET(socket.get(), &readSet);
        timeval timeout{0, 100'000};
#ifdef _WIN32
        const int ready = select(0, &readSet, nullptr, nullptr, &timeout);
#else
        const int ready = select(socket.get() + 1, &readSet, nullptr, nullptr, &timeout);
#endif
        const auto now = grandmp::server::SessionTable::Clock::now();
        for (const auto id : sessions.expire(now)) {
            broadcast(socket.get(), sessions, id, grandmp::protocol::RemoteLeave{id});
            std::cout << "Sessão " << id << " expirada.\n";
        }
        if (ready < 0) {
#ifndef _WIN32
            if (errno == EINTR && stopRequested) break;
#endif
            std::cerr << "Falha no socket UDP.\n";
            return 2;
        }
        if (ready == 0 || !FD_ISSET(socket.get(), &readSet)) continue;

        sockaddr_in remote{};
#ifdef _WIN32
        int remoteLength = sizeof(remote);
#else
        socklen_t remoteLength = sizeof(remote);
#endif
        const int received = recvfrom(socket.get(), reinterpret_cast<char*>(packet.data()),
                                      static_cast<int>(packet.size()), 0,
                                      reinterpret_cast<sockaddr*>(&remote), &remoteLength);
        if (received < 0) continue;  // Inclui datagramas grandes rejeitados pelo Winsock.
        if (remote.sin_family != AF_INET || remoteLength != sizeof(remote)) continue;
        const auto incoming=endpointFrom(remote);
        const bool acceptedSource=!bindSet ? incoming.address==grandmp::core::loopbackAddress :
            grandmp::core::loopbackIpv4(bindAddress) ? grandmp::core::loopbackIpv4(incoming.address) :
            grandmp::core::privateIpv4(incoming.address);
        if(!acceptedSource) continue;
        const auto decoded = grandmp::protocol::decode(
            std::span<const std::uint8_t>(packet.data(), static_cast<std::size_t>(received)));
        if (!decoded.message) {
            if (now - lastRejectLog >= std::chrono::seconds(1)) {
                std::cerr << "Pacote UDP inválido recusado.\n";
                lastRejectLog = now;
            }
            continue;
        }
        const auto endpoint = endpointFrom(remote);
        const auto event = sessions.handle(*decoded.message, endpoint, now);
        // Nao ignorar remocao implicita quando um mesmo endpoint inicia outra sessao.
        for (const auto oldId : event.departedIds)
            broadcast(socket.get(), sessions, oldId,
                      grandmp::protocol::RemoteLeave{oldId});
        if (event.response) (void)sendMessage(socket.get(), endpoint, *event.response);
        using grandmp::server::EventKind;
        switch (event.kind) {
        case EventKind::connected:
            // Welcome primeiro; depois snapshot de quem ja estava presente.
            sendSnapshot(socket.get(), sessions, event.sessionId, endpoint);
            broadcast(socket.get(), sessions, event.sessionId,
                      grandmp::protocol::RemoteJoin{event.sessionId});
            std::cout << "Sessão " << event.sessionId << " conectada ("
                      << unsigned(endpoint.address[0]) << '.' << unsigned(endpoint.address[1]) << '.'
                      << unsigned(endpoint.address[2]) << '.' << unsigned(endpoint.address[3]) << ':'
                      << endpoint.port << ").\n";
            break;
        case EventKind::welcome_repeated:
            sendSnapshot(socket.get(), sessions, event.sessionId, endpoint);
            break;
        case EventKind::snapshot_requested:
        case EventKind::on_foot_subscribed:
            sendSnapshot(socket.get(), sessions, event.sessionId, endpoint);
            break;
        case EventKind::position_accepted:
        case EventKind::on_foot_accepted:
            if (event.position) {
                const auto& p = *event.position;
                broadcast(socket.get(), sessions, event.sessionId,
                    grandmp::protocol::RemotePosition{event.sessionId, p.sequence,
                                                      p.x, p.y, p.z});
                std::cout << std::fixed << std::setprecision(3) << "Sessão " << event.sessionId
                          << " seq=" << p.sequence << " X=" << p.x << " Y=" << p.y
                          << " Z=" << p.z << '\n';
            }
            if (event.onFoot) {
                for (const auto& peer : sessions.peersExcept(event.sessionId)) {
                    if (peer.onFootSubscribed)
                        (void)sendMessage(socket.get(),peer.endpoint,grandmp::protocol::RemoteOnFoot{
                            peer.id,event.sessionId,event.onFoot->sequence,event.onFoot->state});
                }
            }
            break;
        case EventKind::disconnected:
            broadcast(socket.get(), sessions, event.sessionId,
                      grandmp::protocol::RemoteLeave{event.sessionId});
            std::cout << "Sessão " << event.sessionId << " desconectada.\n";
            break;
        case EventKind::rejected_unknown_session:
        case EventKind::rejected_endpoint:
        case EventKind::rejected_sequence:
        case EventKind::rejected_rate:
        case EventKind::rejected_capacity:
            if (now - lastRejectLog >= std::chrono::seconds(1)) {
                std::cerr << "Pacote de sessão recusado (" << static_cast<int>(event.kind)
                          << ").\n";
                lastRejectLog = now;
            }
            break;
        default: break;
        }
    }
    std::cout << "Servidor encerrado.\n";
    return 0;
}
