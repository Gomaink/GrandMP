#include "grandmp/server/sessions.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>

namespace {

#ifdef _WIN32
using SocketValue = SOCKET;
constexpr SocketValue invalidSocket = INVALID_SOCKET;
#else
using SocketValue = int;
constexpr SocketValue invalidSocket = -1;
#endif

class Socket {
public:
    Socket() : value_(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) {}
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
    SocketValue get() const { return value_; }
    bool valid() const { return value_ != invalidSocket; }
private:
    SocketValue value_;
};

sockaddr_in loopback(std::uint16_t port) {
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    return address;
}

bool bindEphemeral(const Socket& socket, sockaddr_in& address) {
    address = loopback(0);
    if (::bind(socket.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0)
        return false;
#ifdef _WIN32
    int length = sizeof(address);
#else
    socklen_t length = sizeof(address);
#endif
    return getsockname(socket.get(), reinterpret_cast<sockaddr*>(&address), &length) == 0;
}

bool send(const Socket& from, const sockaddr_in& to, const grandmp::protocol::Message& message) {
    const auto packet = grandmp::protocol::encode(message);
    return packet && sendto(from.get(), reinterpret_cast<const char*>(packet->data()),
                            static_cast<int>(packet->size()), 0,
                            reinterpret_cast<const sockaddr*>(&to), sizeof(to)) ==
                         static_cast<int>(packet->size());
}

std::optional<grandmp::protocol::Message> receive(const Socket& socket, sockaddr_in& from) {
    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(socket.get(), &readSet);
    timeval timeout{1, 0};
#ifdef _WIN32
    if (select(0, &readSet, nullptr, nullptr, &timeout) != 1) return std::nullopt;
    int length = sizeof(from);
#else
    if (select(socket.get() + 1, &readSet, nullptr, nullptr, &timeout) != 1)
        return std::nullopt;
    socklen_t length = sizeof(from);
#endif
    std::array<std::uint8_t, 64> bytes{};
    const int count = recvfrom(socket.get(), reinterpret_cast<char*>(bytes.data()),
                               static_cast<int>(bytes.size()), 0,
                               reinterpret_cast<sockaddr*>(&from), &length);
    if (count < 0) return std::nullopt;
    return grandmp::protocol::decode(std::span<const std::uint8_t>(
        bytes.data(), static_cast<std::size_t>(count))).message;
}

grandmp::server::Endpoint endpointFrom(const sockaddr_in& address) {
    const auto ip = ntohl(address.sin_addr.s_addr);
    return {{{static_cast<std::uint8_t>(ip >> 24U),
              static_cast<std::uint8_t>(ip >> 16U),
              static_cast<std::uint8_t>(ip >> 8U),
              static_cast<std::uint8_t>(ip)}}, ntohs(address.sin_port)};
}

bool exchange(const Socket& serverSocket, const Socket& client,
              const sockaddr_in& serverAddress, grandmp::server::SessionTable& table,
              const grandmp::protocol::Message& message,
              grandmp::server::EventKind expected,
              grandmp::server::SessionTable::Time time,
              grandmp::server::Event& event) {
    if (!send(client, serverAddress, message)) return false;
    sockaddr_in sender{};
    const auto incoming = receive(serverSocket, sender);
    if (!incoming) return false;
    event = table.handle(*incoming, endpointFrom(sender), time);
    if (event.kind != expected) return false;
    if (event.response && !send(serverSocket, sender, *event.response)) return false;
    return true;
}

bool loopbackTwoClients() {
#ifdef _WIN32
    WSADATA data{};
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0) return false;
#endif
    bool passed = false;
    {
        Socket serverSocket;
        Socket clientA;
        Socket clientB;
        sockaddr_in serverAddress{};
        sockaddr_in aAddress{};
        sockaddr_in bAddress{};
        if (serverSocket.valid() && clientA.valid() && clientB.valid() &&
            bindEphemeral(serverSocket, serverAddress) && bindEphemeral(clientA, aAddress) &&
            bindEphemeral(clientB, bAddress)) {
            grandmp::server::SessionTable table(100);
            const auto start = grandmp::server::SessionTable::Time{};
            grandmp::server::Event eventA;
            grandmp::server::Event eventB;
            sockaddr_in source{};
            passed = exchange(serverSocket, clientA, serverAddress, table,
                              grandmp::protocol::Hello{11},
                              grandmp::server::EventKind::connected, start, eventA) &&
                     receive(clientA, source).has_value() &&
                     exchange(serverSocket, clientB, serverAddress, table,
                              grandmp::protocol::Hello{22},
                              grandmp::server::EventKind::connected, start, eventB) &&
                     receive(clientB, source).has_value() &&
                     eventA.sessionId != eventB.sessionId &&
                     exchange(serverSocket, clientA, serverAddress, table,
                              grandmp::protocol::PlayerPosition{eventA.sessionId, 1, 1, 2, 3},
                              grandmp::server::EventKind::position_accepted, start, eventA) &&
                     exchange(serverSocket, clientB, serverAddress, table,
                              grandmp::protocol::PlayerPosition{eventB.sessionId, 1, 4, 5, 6},
                              grandmp::server::EventKind::position_accepted, start, eventB) &&
                     eventA.position && eventB.position && eventA.position->x == 1 &&
                     eventB.position->x == 4 &&
                     exchange(serverSocket, clientA, serverAddress, table,
                              grandmp::protocol::Disconnect{eventA.sessionId},
                              grandmp::server::EventKind::disconnected, start, eventA) &&
                     table.size() == 1;
        }
    }
#ifdef _WIN32
    WSACleanup();
#endif
    return passed;
}

}  // namespace

int main() { return loopbackTwoClients() ? 0 : 1; }
