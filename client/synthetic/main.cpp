#include "grandmp/core/protocol.hpp"
#include "grandmp/core/remote_roster.hpp"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <csignal>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <span>
#include <string_view>
#include <variant>

namespace {
#ifdef _WIN32
using SocketValue = SOCKET;
constexpr SocketValue invalidSocket = INVALID_SOCKET;
volatile LONG stopFlag = 0;
BOOL WINAPI onControl(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT || event == CTRL_CLOSE_EVENT) {
        InterlockedExchange(&stopFlag, 1); return TRUE;
    }
    return FALSE;
}
bool stopped() { return InterlockedCompareExchange(&stopFlag, 0, 0) != 0; }
#else
using SocketValue = int;
constexpr SocketValue invalidSocket = -1;
volatile std::sig_atomic_t stopFlag = 0;
void onSignal(int) { stopFlag = 1; }
bool stopped() { return stopFlag != 0; }
#endif

using Clock = std::chrono::steady_clock;

class SocketRuntime {
public:
    SocketRuntime() {
#ifdef _WIN32
        WSADATA data{};
        ready_ = WSAStartup(MAKEWORD(2, 2), &data) == 0;
#else
        ready_ = true;
#endif
    }
    ~SocketRuntime() {
#ifdef _WIN32
        if (ready_) WSACleanup();
#endif
    }
    bool ready() const { return ready_; }
private:
    bool ready_ = false;
};

class SocketGuard {
public:
    explicit SocketGuard(SocketValue s) : value_(s) {}
    ~SocketGuard() {
        if (value_ != invalidSocket) {
#ifdef _WIN32
            closesocket(value_);
#else
            close(value_);
#endif
        }
    }
    SocketValue get() const { return value_; }
    bool valid() const { return value_ != invalidSocket; }
private:
    SocketValue value_;
};

bool sendPacket(SocketValue sock, const sockaddr_in& dest,
                const grandmp::protocol::Message& message) {
    const auto bytes = grandmp::protocol::encode(message);
    if (!bytes) return false;
    return sendto(sock, reinterpret_cast<const char*>(bytes->data()),
                  static_cast<int>(bytes->size()), 0,
                  reinterpret_cast<const sockaddr*>(&dest), sizeof(dest)) ==
           static_cast<int>(bytes->size());
}

bool parsePort(int argc, char** argv, std::uint16_t& port) {
    if (argc == 1) return true;
    if (argc != 3 || std::string_view(argv[1]) != "--port") return false;
    const char* s = argv[2];
    if (!*s) return false;
    unsigned number = 0;
    while (*s) {
        if (*s < '0' || *s > '9') return false;
        number = number * 10 + static_cast<unsigned>(*s++ - '0');
        if (number > 65535) return false;
    }
    if (number == 0) return false;
    port = static_cast<std::uint16_t>(number);
    return true;
}
}

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--help") {
        std::puts("Uso: grandmp_synthetic_client [--port 7777] (127.0.0.1, Ctrl+C encerra)");
        return 0;
    }
    std::uint16_t port = 7777;
    if (!parsePort(argc, argv, port)) return 2;
    SocketRuntime runtime;
    if (!runtime.ready()) return 2;
    SocketGuard socket(::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP));
    if (!socket.valid()) return 2;
#ifdef _WIN32
    if (!SetConsoleCtrlHandler(onControl, TRUE)) return 2;
#else
    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
#endif
    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server.sin_port = htons(port);
    std::random_device random;
    std::uint64_t nonce = (static_cast<std::uint64_t>(random()) << 32) | random();
    if (nonce == 0) nonce = 1;
    std::uint64_t id = 0;
    std::uint32_t sequence = 0;
    grandmp::core::RemoteRoster roster;
    auto lastHello = Clock::time_point{};
    auto lastSnapshot = Clock::time_point{};
    auto lastWelcome = Clock::time_point{};
    auto lastPosition = Clock::time_point{};
    std::puts("GrandMP Synthetic Client -- apenas loopback. Ctrl+C encerra.");
    while (!stopped()) {
        const auto now = Clock::now();
        if (lastHello == Clock::time_point{} || now - lastHello >= std::chrono::seconds(3)) {
            (void)sendPacket(socket.get(), server, grandmp::protocol::Hello{nonce});
            lastHello = now;
        }
        if (id && now - lastWelcome >= std::chrono::seconds(10)) {
            id = 0;
            sequence = 0;
            roster.clear();
        }
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(socket.get(), &readable);
        timeval timeout{0, 50'000};
#ifdef _WIN32
        const int ready = select(0, &readable, nullptr, nullptr, &timeout);
#else
        const int ready = select(socket.get() + 1, &readable, nullptr, nullptr, &timeout);
#endif
        if (ready > 0) {
            std::array<std::uint8_t, grandmp::protocol::kMaxPacketSize + 1> bytes{};
            sockaddr_in from{};
#ifdef _WIN32
            int fromLen = sizeof(from);
#else
            socklen_t fromLen = sizeof(from);
#endif
            const int count = recvfrom(socket.get(), reinterpret_cast<char*>(bytes.data()),
                static_cast<int>(bytes.size()), 0, reinterpret_cast<sockaddr*>(&from), &fromLen);
            if (count >= 0 && fromLen == sizeof(from) &&
                from.sin_addr.s_addr == server.sin_addr.s_addr && from.sin_port == server.sin_port) {
                auto decoded = grandmp::protocol::decode(std::span<const std::uint8_t>(
                    bytes.data(), static_cast<std::size_t>(count)));
                if (decoded.message) {
                    using namespace grandmp::protocol;
                    if (const auto* welcome = std::get_if<Welcome>(&*decoded.message)) {
                        if (welcome->nonce == nonce) {
                            if (id != welcome->session_id) {
                                id = welcome->session_id;
                                sequence = 0;
                                roster.clear();
                                lastSnapshot = Clock::time_point{};
                                std::printf("Sessao %llu estabelecida.\n",
                                    static_cast<unsigned long long>(id));
                            }
                            lastWelcome = Clock::now();
                        }
                    } else if (id) {
                        const auto timestamp = Clock::now();
                        if (const auto* join = std::get_if<RemoteJoin>(&*decoded.message)) {
                            if (join->player_id != id && roster.joined(*join, timestamp))
                                std::printf("Remoto %llu entrou.\n",
                                    static_cast<unsigned long long>(join->player_id));
                        } else if (const auto* pos = std::get_if<RemotePosition>(&*decoded.message)) {
                            if (pos->player_id == id) continue;
                            const auto result = roster.position(*pos, timestamp);
                            if (result == grandmp::core::RemoteRoster::PositionResult::added)
                                std::printf("Remoto %llu entrou.\n",
                                    static_cast<unsigned long long>(pos->player_id));
                            if (result == grandmp::core::RemoteRoster::PositionResult::added ||
                                result == grandmp::core::RemoteRoster::PositionResult::updated)
                                std::printf("Remoto %llu seq=%u X=%.3f Y=%.3f Z=%.3f\n",
                                    static_cast<unsigned long long>(pos->player_id), pos->sequence,
                                    pos->x, pos->y, pos->z);
                        } else if (const auto* leave = std::get_if<RemoteLeave>(&*decoded.message)) {
                            if (roster.left(*leave, timestamp))
                                std::printf("Remoto %llu saiu.\n",
                                    static_cast<unsigned long long>(leave->player_id));
                        }
                    }
                }
            }
        }
        const auto after = Clock::now();
        if (id && (lastSnapshot == Clock::time_point{} ||
                   after - lastSnapshot >= std::chrono::seconds(3))) {
            (void)sendPacket(socket.get(), server, grandmp::protocol::SnapshotRequest{id});
            lastSnapshot = after;
        }
        if (id && (lastPosition == Clock::time_point{} ||
                   after - lastPosition >= std::chrono::milliseconds(200))) {
            ++sequence;
            // Movimento sintetico lento, sempre dentro dos limites validados.
            const float x = 100.0f + static_cast<float>(sequence % 1000) / 10.0f;
            (void)sendPacket(socket.get(), server, grandmp::protocol::PlayerPosition{
                id, sequence, x, 200.0f, 30.0f});
            lastPosition = after;
        }
        for (const auto expired : roster.expire(after))
            std::printf("Remoto %llu expirou.\n",
                        static_cast<unsigned long long>(expired));
    }
    if (id) (void)sendPacket(socket.get(), server, grandmp::protocol::Disconnect{id});
    std::puts("Cliente sintetico encerrado.");
    return 0;
}
