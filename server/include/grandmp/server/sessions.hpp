#pragma once

#include "grandmp/core/protocol.hpp"

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <vector>

namespace grandmp::server {

struct Endpoint {
    std::array<std::uint8_t, 4> address{};
    std::uint16_t port = 0;

    auto operator<=>(const Endpoint&) const = default;
};

enum class EventKind {
    connected,
    welcome_repeated,
    position_accepted,
    disconnected,
    snapshot_requested,
    rejected_unknown_session,
    rejected_endpoint,
    rejected_sequence,
    rejected_rate,
    rejected_capacity,
    ignored,
    on_foot_accepted,
    on_foot_subscribed,
};

struct Event {
    EventKind kind = EventKind::ignored;
    std::uint64_t sessionId = 0;
    std::optional<protocol::Message> response{};
    std::optional<protocol::PlayerPosition> position{};
    std::vector<std::uint64_t> departedIds{}; // Substituicoes por Hello do mesmo endpoint.
    std::optional<protocol::PlayerOnFoot> onFoot{};
};

struct PeerState {
    std::uint64_t id;
    Endpoint endpoint;
    std::optional<protocol::PlayerPosition> position{};
    std::optional<protocol::PlayerOnFoot> onFoot{};
    bool onFootSubscribed = false;
};

class SessionTable {
public:
    using Clock = std::chrono::steady_clock;
    using Time = Clock::time_point;

    explicit SessionTable(std::uint64_t firstSessionId = 1) noexcept;

    [[nodiscard]] Event handle(const protocol::Message& message, Endpoint endpoint, Time now);
    [[nodiscard]] std::vector<std::uint64_t> expire(Time now);
    [[nodiscard]] std::size_t size() const noexcept { return sessions_.size(); }
    [[nodiscard]] std::vector<PeerState> peersExcept(std::uint64_t excludedId) const;
    [[nodiscard]] bool onFootSubscribed(std::uint64_t id) const noexcept;
    [[nodiscard]] std::optional<protocol::PlayerOnFoot> recentOnFoot(std::uint64_t id, Time now) const noexcept;

    static constexpr std::size_t maxSessions = 64;
    static constexpr auto sessionTimeout = std::chrono::seconds(10);
    static constexpr auto minPositionInterval = std::chrono::milliseconds(100);

private:
    struct Session {
        Endpoint endpoint;
        std::uint64_t nonce = 0;
        std::uint32_t lastSequence = 0;
        Time lastActivity{};
        Time lastPosition{};
        bool hasPosition = false;
        std::optional<protocol::PlayerPosition> position{};
        Time lastSnapshot{};
        bool hasRequestedSnapshot = false;
        std::optional<protocol::PlayerOnFoot> onFoot{};
        bool onFootSubscribed = false;
    };

    std::map<std::uint64_t, Session> sessions_;
    std::uint64_t nextSessionId_;
};

}  // namespace grandmp::server
