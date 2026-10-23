#pragma once

#include "grandmp/core/player_input.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace grandmp::protocol {

inline constexpr std::uint8_t kProtocolVersion = 1;
inline constexpr std::size_t kHeaderSize = 8;
inline constexpr std::size_t kMaxPacketSize = 80;
inline constexpr float kMaxCoordinate = 100000.0f;

enum class MessageType : std::uint8_t {
    hello = 1,
    welcome = 2,
    player_position = 3,
    disconnect = 4,
    remote_join = 5,
    remote_position = 6,
    remote_leave = 7,
    snapshot_request = 8,
    player_on_foot = 9,
    remote_on_foot = 10,
    on_foot_subscribe = 11,
};

struct Hello {
    std::uint64_t nonce;
    bool operator==(const Hello&) const = default;
};

struct Welcome {
    std::uint64_t nonce;
    std::uint64_t session_id;
    bool operator==(const Welcome&) const = default;
};

struct PlayerPosition {
    std::uint64_t session_id;
    std::uint32_t sequence;
    float x;
    float y;
    float z;
    bool operator==(const PlayerPosition&) const = default;
};

struct Disconnect {
    std::uint64_t session_id;
    bool operator==(const Disconnect&) const = default;
};

// Mensagens 5-8 estendem o wire format v1, sem mudar as mensagens 1-4.
// Clientes 0.3.1 simplesmente ignoram tipos que nao conhecem.
struct RemoteJoin { std::uint64_t player_id; bool operator==(const RemoteJoin&) const = default; };
struct RemotePosition {
    std::uint64_t player_id;
    std::uint32_t sequence;
    float x, y, z;
    bool operator==(const RemotePosition&) const = default;
};
struct RemoteLeave { std::uint64_t player_id; bool operator==(const RemoteLeave&) const = default; };
struct SnapshotRequest { std::uint64_t session_id; bool operator==(const SnapshotRequest&) const = default; };

// Optional v1 extension: messages 1-8 retain their exact bytes. Only a peer that
// explicitly subscribes receives type 10. The recipient session rejects replay
// from a previous connection before the controller sees any controls.
struct PlayerOnFoot {
    std::uint64_t session_id;
    std::uint32_t sequence;
    core::OnFootState state;
    bool operator==(const PlayerOnFoot&) const = default;
};
struct RemoteOnFoot {
    std::uint64_t recipient_session, player_id;
    std::uint32_t sequence;
    core::OnFootState state;
    bool operator==(const RemoteOnFoot&) const = default;
};
struct OnFootSubscribe { std::uint64_t session_id; bool operator==(const OnFootSubscribe&) const = default; };

using Message = std::variant<Hello, Welcome, PlayerPosition, Disconnect,
                             RemoteJoin, RemotePosition, RemoteLeave, SnapshotRequest,
                             PlayerOnFoot, RemoteOnFoot, OnFootSubscribe>;

enum class DecodeStatus {
    ok,
    invalid_size,
    invalid_magic,
    unsupported_version,
    invalid_type,
    invalid_field,
};

struct DecodeResult {
    DecodeStatus status;
    std::optional<Message> message;
};

// Integers use big-endian network byte order. Float payloads use IEEE-754
// binary32 bit patterns, serialized as big-endian unsigned 32-bit integers.
// The datagram must contain exactly one message and no trailing bytes.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> encode(const Message& message);
[[nodiscard]] DecodeResult decode(std::span<const std::uint8_t> datagram) noexcept;
[[nodiscard]] bool validCoordinates(float x, float y, float z) noexcept;

}  // namespace grandmp::protocol
