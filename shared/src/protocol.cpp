#include "grandmp/core/protocol.hpp"

#include <bit>
#include <cmath>
#include <limits>
#include <type_traits>

namespace grandmp::protocol {
namespace {

static_assert(sizeof(float) == sizeof(std::uint32_t));
static_assert(std::numeric_limits<float>::is_iec559);

constexpr std::uint8_t kMagic[] = {'G', 'M', 'P', '1'};

void appendU16(std::vector<std::uint8_t>& bytes, std::uint16_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value >> 8));
    bytes.push_back(static_cast<std::uint8_t>(value));
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    for (int shift = 24; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

void appendU64(std::vector<std::uint8_t>& bytes, std::uint64_t value) {
    for (int shift = 56; shift >= 0; shift -= 8) {
        bytes.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset) {
    return static_cast<std::uint16_t>((std::uint16_t{bytes[offset]} << 8) |
                                      bytes[offset + 1]);
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint32_t value = 0;
    for (std::size_t i = 0; i < 4; ++i) {
        value = (value << 8) | bytes[offset + i];
    }
    return value;
}

std::uint64_t readU64(std::span<const std::uint8_t> bytes, std::size_t offset) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) {
        value = (value << 8) | bytes[offset + i];
    }
    return value;
}

}  // namespace

bool validCoordinates(float x, float y, float z) noexcept {
    return std::isfinite(x) && std::isfinite(y) && std::isfinite(z) &&
           std::fabs(x) <= kMaxCoordinate && std::fabs(y) <= kMaxCoordinate &&
           std::fabs(z) <= kMaxCoordinate;
}

std::optional<std::vector<std::uint8_t>> encode(const Message& message) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(kMaxPacketSize);
    for (const auto value : kMagic) {
        bytes.push_back(value);
    }
    bytes.push_back(kProtocolVersion);

    const bool valid = std::visit([&bytes](const auto& value) {
        using T = std::decay_t<decltype(value)>;
        if constexpr (std::is_same_v<T, Hello>) {
            if (value.nonce == 0) return false;
            bytes.push_back(static_cast<std::uint8_t>(MessageType::hello));
            appendU16(bytes, 8);
            appendU64(bytes, value.nonce);
        } else if constexpr (std::is_same_v<T, Welcome>) {
            if (value.nonce == 0 || value.session_id == 0) return false;
            bytes.push_back(static_cast<std::uint8_t>(MessageType::welcome));
            appendU16(bytes, 16);
            appendU64(bytes, value.nonce);
            appendU64(bytes, value.session_id);
        } else if constexpr (std::is_same_v<T, PlayerPosition>) {
            if (value.session_id == 0 || value.sequence == 0 ||
                !validCoordinates(value.x, value.y, value.z)) return false;
            bytes.push_back(static_cast<std::uint8_t>(MessageType::player_position));
            appendU16(bytes, 24);
            appendU64(bytes, value.session_id);
            appendU32(bytes, value.sequence);
            appendU32(bytes, std::bit_cast<std::uint32_t>(value.x));
            appendU32(bytes, std::bit_cast<std::uint32_t>(value.y));
            appendU32(bytes, std::bit_cast<std::uint32_t>(value.z));
        } else if constexpr (std::is_same_v<T, Disconnect>) {
            if (value.session_id == 0) return false;
            bytes.push_back(static_cast<std::uint8_t>(MessageType::disconnect));
            appendU16(bytes, 8);
            appendU64(bytes, value.session_id);
        } else if constexpr (std::is_same_v<T, RemoteJoin> || std::is_same_v<T, RemoteLeave>) {
            if (value.player_id == 0) return false;
            bytes.push_back(static_cast<std::uint8_t>(std::is_same_v<T, RemoteJoin>
                ? MessageType::remote_join : MessageType::remote_leave));
            appendU16(bytes, 8);
            appendU64(bytes, value.player_id);
        } else if constexpr (std::is_same_v<T, RemotePosition>) {
            if (value.player_id == 0 || value.sequence == 0 ||
                !validCoordinates(value.x, value.y, value.z)) return false;
            bytes.push_back(static_cast<std::uint8_t>(MessageType::remote_position));
            appendU16(bytes, 24);
            appendU64(bytes, value.player_id);
            appendU32(bytes, value.sequence);
            appendU32(bytes, std::bit_cast<std::uint32_t>(value.x));
            appendU32(bytes, std::bit_cast<std::uint32_t>(value.y));
            appendU32(bytes, std::bit_cast<std::uint32_t>(value.z));
        } else if constexpr (std::is_same_v<T, SnapshotRequest>) {
            if (value.session_id == 0) return false;
            bytes.push_back(static_cast<std::uint8_t>(MessageType::snapshot_request));
            appendU16(bytes, 8);
            appendU64(bytes, value.session_id);
        }
        return true;
    }, message);
    if (!valid) return std::nullopt;
    return bytes;
}

DecodeResult decode(std::span<const std::uint8_t> bytes) noexcept {
    if (bytes.size() < kHeaderSize || bytes.size() > kMaxPacketSize) {
        return {DecodeStatus::invalid_size, std::nullopt};
    }
    for (std::size_t i = 0; i < 4; ++i) {
        if (bytes[i] != kMagic[i]) return {DecodeStatus::invalid_magic, std::nullopt};
    }
    if (bytes[4] != kProtocolVersion) {
        return {DecodeStatus::unsupported_version, std::nullopt};
    }
    const auto type = static_cast<MessageType>(bytes[5]);
    std::size_t expected_payload = 0;
    switch (type) {
        case MessageType::hello: expected_payload = 8; break;
        case MessageType::welcome: expected_payload = 16; break;
        case MessageType::player_position: expected_payload = 24; break;
        case MessageType::disconnect: expected_payload = 8; break;
        case MessageType::remote_join: expected_payload = 8; break;
        case MessageType::remote_position: expected_payload = 24; break;
        case MessageType::remote_leave: expected_payload = 8; break;
        case MessageType::snapshot_request: expected_payload = 8; break;
        default: return {DecodeStatus::invalid_type, std::nullopt};
    }
    if (readU16(bytes, 6) != expected_payload || bytes.size() != kHeaderSize + expected_payload) {
        return {DecodeStatus::invalid_size, std::nullopt};
    }
    switch (type) {
        case MessageType::hello: {
            const Hello value{readU64(bytes, 8)};
            if (value.nonce == 0) return {DecodeStatus::invalid_field, std::nullopt};
            return {DecodeStatus::ok, Message{value}};
        }
        case MessageType::welcome: {
            const Welcome value{readU64(bytes, 8), readU64(bytes, 16)};
            if (value.nonce == 0 || value.session_id == 0) {
                return {DecodeStatus::invalid_field, std::nullopt};
            }
            return {DecodeStatus::ok, Message{value}};
        }
        case MessageType::player_position: {
            const PlayerPosition value{
                readU64(bytes, 8), readU32(bytes, 16),
                std::bit_cast<float>(readU32(bytes, 20)),
                std::bit_cast<float>(readU32(bytes, 24)),
                std::bit_cast<float>(readU32(bytes, 28))};
            if (value.session_id == 0 || value.sequence == 0 ||
                !validCoordinates(value.x, value.y, value.z)) {
                return {DecodeStatus::invalid_field, std::nullopt};
            }
            return {DecodeStatus::ok, Message{value}};
        }
        case MessageType::disconnect: {
            const Disconnect value{readU64(bytes, 8)};
            if (value.session_id == 0) return {DecodeStatus::invalid_field, std::nullopt};
            return {DecodeStatus::ok, Message{value}};
        }
        case MessageType::remote_join:
        case MessageType::remote_leave: {
            const auto id = readU64(bytes, 8);
            if (id == 0) return {DecodeStatus::invalid_field, std::nullopt};
            if (type == MessageType::remote_join)
                return {DecodeStatus::ok, Message{RemoteJoin{id}}};
            return {DecodeStatus::ok, Message{RemoteLeave{id}}};
        }
        case MessageType::remote_position: {
            const RemotePosition value{
                readU64(bytes, 8), readU32(bytes, 16),
                std::bit_cast<float>(readU32(bytes, 20)),
                std::bit_cast<float>(readU32(bytes, 24)),
                std::bit_cast<float>(readU32(bytes, 28))};
            if (value.player_id == 0 || value.sequence == 0 ||
                !validCoordinates(value.x, value.y, value.z))
                return {DecodeStatus::invalid_field, std::nullopt};
            return {DecodeStatus::ok, Message{value}};
        }
        case MessageType::snapshot_request: {
            const auto id = readU64(bytes, 8);
            if (id == 0) return {DecodeStatus::invalid_field, std::nullopt};
            return {DecodeStatus::ok, Message{SnapshotRequest{id}}};
        }
        default: return {DecodeStatus::invalid_type, std::nullopt};
    }
}

}  // namespace grandmp::protocol
