#include <wok/session.hpp>

#include <algorithm>

namespace wok {

SessionManager::SessionManager(SessionConfig cfg) : cfg_(std::move(cfg)) {}

std::vector<Outgoing> SessionManager::onConnect(ConnId conn, double nowSec) {
    Conn c;
    c.lastSeen = nowSec;
    conns_[conn] = c;
    return {};
}

std::vector<Outgoing> SessionManager::onPacket(ConnId conn, std::span<const uint8_t> bytes, double nowSec) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return {};
    Conn& c = it->second;
    c.lastSeen = nowSec;

    auto msg = decode(bytes);
    if (!msg) {
        if (++c.badPackets >= cfg_.maxBadPackets) requestClose(conn);
        return {};
    }
    if (auto* h = std::get_if<Hello>(&*msg)) {
        if (c.player) return {};  // duplicate Hello: ignore
        return handleHello(conn, c, *h);
    }
    if (auto* s = std::get_if<PlayerState>(&*msg)) {
        if (!c.player) return {};  // Hello required first
        return handleState(conn, c, *s);
    }
    return {};  // server-to-client messages are ignored when received from a client
}

std::vector<Outgoing> SessionManager::handleHello(ConnId conn, Conn& c, const Hello& h) {
    RejectReason reason = RejectReason::None;
    if (h.protocolVersion != WOK_PROTOCOL_VERSION)
        reason = RejectReason::VersionMismatch;
    else if (cfg_.bannedSteamIds.contains(h.steamId))
        reason = RejectReason::Banned;
    else if (playerCount() >= cfg_.maxPlayers)
        reason = RejectReason::ServerFull;

    if (reason != RejectReason::None) {
        requestClose(conn);
        return {Outgoing{conn, HelloAck{reason, 0}}};
    }

    c.player = nextPlayerId_++;
    std::vector<Outgoing> out{Outgoing{conn, HelloAck{RejectReason::None, *c.player}}};
    for (auto& [otherId, other] : conns_)
        if (otherId != conn && other.player && other.lastState) out.push_back({conn, *other.lastState});
    return out;
}

std::vector<Outgoing> SessionManager::handleState(ConnId conn, Conn& c, PlayerState s) {
    s.playerId = *c.player;  // never trust the id claimed by the client
    c.lastState = s;
    std::vector<Outgoing> out;
    for (auto& [otherId, other] : conns_)
        if (otherId != conn && other.player) out.push_back({otherId, s});
    return out;
}

std::vector<Outgoing> SessionManager::dropConn(ConnId conn) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return {};
    std::optional<PlayerId> player = it->second.player;
    conns_.erase(it);
    std::vector<Outgoing> out;
    if (player)
        for (auto& [otherId, other] : conns_)
            if (other.player) out.push_back({otherId, PlayerLeft{*player}});
    return out;
}

std::vector<Outgoing> SessionManager::onDisconnect(ConnId conn) { return dropConn(conn); }

std::vector<Outgoing> SessionManager::tick(double nowSec) {
    std::vector<ConnId> expired;
    for (auto& [id, c] : conns_)
        if (nowSec - c.lastSeen > cfg_.timeoutSec) expired.push_back(id);
    std::vector<Outgoing> out;
    for (ConnId id : expired) {
        auto left = dropConn(id);
        out.insert(out.end(), left.begin(), left.end());
        requestClose(id);
    }
    return out;
}

void SessionManager::requestClose(ConnId conn) {
    if (std::find(toClose_.begin(), toClose_.end(), conn) == toClose_.end()) toClose_.push_back(conn);
}

std::vector<ConnId> SessionManager::connectionsToClose() { return std::exchange(toClose_, {}); }

size_t SessionManager::playerCount() const {
    return static_cast<size_t>(std::count_if(conns_.begin(), conns_.end(), [](auto& kv) { return kv.second.player.has_value(); }));
}

}  // namespace wok
