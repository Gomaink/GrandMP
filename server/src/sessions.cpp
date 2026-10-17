#include "grandmp/server/sessions.hpp"

#include <algorithm>
#include <limits>
#include <variant>

namespace grandmp::server {

SessionTable::SessionTable(std::uint64_t firstSessionId) noexcept
    : nextSessionId_(firstSessionId == 0 ? 1 : firstSessionId) {}

Event SessionTable::handle(const protocol::Message& message, Endpoint endpoint, Time now) {
    if (const auto* hello = std::get_if<protocol::Hello>(&message)) {
        for (auto& [id, session] : sessions_) {
            if (session.endpoint == endpoint && session.nonce == hello->nonce) {
                session.lastActivity = now;
                return {EventKind::welcome_repeated, id,
                        protocol::Message{protocol::Welcome{hello->nonce, id}}, std::nullopt};
            }
        }
        // Uma nova conexão do mesmo endpoint substitui a sessão anterior.
        // Verificar capacidade antes de substituir a sessao antiga do endpoint.
        bool alreadyHere = false;
        for (const auto& [id, session] : sessions_) {
            if (session.endpoint == endpoint) { alreadyHere = true; break; }
        }
        if ((sessions_.size() >= maxSessions && !alreadyHere) ||
            nextSessionId_ == std::numeric_limits<std::uint64_t>::max()) {
            return {EventKind::rejected_capacity};
        }
        Event result{EventKind::connected};
        for (auto it = sessions_.begin(); it != sessions_.end();) {
            if (it->second.endpoint == endpoint) {
                result.departedIds.push_back(it->first);
                it = sessions_.erase(it);
            } else ++it;
        }
        const auto id = nextSessionId_++;
        sessions_.emplace(id, Session{endpoint, hello->nonce, 0, now, {}, false});
        result.sessionId = id;
        result.response = protocol::Message{protocol::Welcome{hello->nonce, id}};
        return result;
    }

    if (const auto* position = std::get_if<protocol::PlayerPosition>(&message)) {
        const auto it = sessions_.find(position->session_id);
        if (it == sessions_.end()) return {EventKind::rejected_unknown_session};
        auto& session = it->second;
        if (session.endpoint != endpoint) return {EventKind::rejected_endpoint, position->session_id};
        // v1 exige sequência estritamente crescente; no overflow o cliente inicia nova sessão.
        if (position->sequence <= session.lastSequence) {
            return {EventKind::rejected_sequence, position->session_id};
        }
        if (session.hasPosition && now - session.lastPosition < minPositionInterval) {
            return {EventKind::rejected_rate, position->session_id};
        }
        session.lastSequence = position->sequence;
        session.lastPosition = now;
        session.lastActivity = now;
        session.hasPosition = true;
        session.position = *position;
        return {EventKind::position_accepted, position->session_id, std::nullopt, *position};
    }

    if (const auto* disconnect = std::get_if<protocol::Disconnect>(&message)) {
        const auto it = sessions_.find(disconnect->session_id);
        if (it == sessions_.end()) return {EventKind::rejected_unknown_session};
        if (it->second.endpoint != endpoint) {
            return {EventKind::rejected_endpoint, disconnect->session_id};
        }
        sessions_.erase(it);
        return {EventKind::disconnected, disconnect->session_id};
    }
    if (const auto* request = std::get_if<protocol::SnapshotRequest>(&message)) {
        const auto it = sessions_.find(request->session_id);
        if (it == sessions_.end()) return {EventKind::rejected_unknown_session};
        auto& session = it->second;
        if (session.endpoint != endpoint)
            return {EventKind::rejected_endpoint, request->session_id};
        if (session.hasRequestedSnapshot && now - session.lastSnapshot < std::chrono::seconds(1))
            return {EventKind::rejected_rate, request->session_id};
        session.lastSnapshot = now;
        session.hasRequestedSnapshot = true;
        session.lastActivity = now;
        return {EventKind::snapshot_requested, request->session_id};
    }
    return {EventKind::ignored};
}

std::vector<PeerState> SessionTable::peersExcept(std::uint64_t excludedId) const {
    std::vector<PeerState> peers;
    peers.reserve(sessions_.size());
    for (const auto& [id, session] : sessions_) {
        if (id != excludedId) peers.push_back({id, session.endpoint, session.position});
    }
    return peers;
}

std::vector<std::uint64_t> SessionTable::expire(Time now) {
    std::vector<std::uint64_t> expired;
    for (auto it = sessions_.begin(); it != sessions_.end();) {
        if (now >= it->second.lastActivity &&
            now - it->second.lastActivity >= sessionTimeout) {
            expired.push_back(it->first);
            it = sessions_.erase(it);
        } else {
            ++it;
        }
    }
    return expired;
}

}  // namespace grandmp::server
