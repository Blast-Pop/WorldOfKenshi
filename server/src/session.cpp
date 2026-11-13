#include <wok/character_store.hpp>
#include <wok/session.hpp>

#include <algorithm>

namespace wok {

namespace {

std::string toHex(const AuthToken& t) {
    static const char* digits = "0123456789abcdef";
    std::string s;
    for (uint8_t b : t) {
        s.push_back(digits[b >> 4]);
        s.push_back(digits[b & 15]);
    }
    return s;
}

}  // namespace

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
        if (c.player || !c.nonce.empty()) return {};  // duplicate Hello: ignore
        return handleHello(conn, c, *h, nowSec);
    }
    if (!c.player) return {};  // everything else requires an accepted player
    if (auto* s = std::get_if<PlayerState>(&*msg)) return handleState(conn, c, *s);
    if (auto* cc = std::get_if<CreateCharacter>(&*msg)) return handleCreate(conn, c, *cc);
    return {};  // server-to-client messages are ignored when received from a client
}

std::vector<Outgoing> SessionManager::reject(ConnId conn, RejectReason reason) {
    requestClose(conn);
    return {Outgoing{conn, HelloAck{reason, 0}}};
}

std::vector<Outgoing> SessionManager::handleHello(ConnId conn, Conn& c, const Hello& h, double nowSec) {
    if (h.protocolVersion != WOK_PROTOCOL_VERSION) return reject(conn, RejectReason::VersionMismatch);
    if (cfg_.bannedSteamIds.contains(h.steamId)) return reject(conn, RejectReason::Banned);
    if (playerCount() >= cfg_.maxPlayers) return reject(conn, RejectReason::ServerFull);

    if (!cfg_.store) return accept(conn, c, AuthToken{}, nowSec);

    if (cfg_.allowTestBots && h.steamId != 0 && h.steamId < kMaxTestBotSteamId) {
        c.steamId = h.steamId;  // test bot: no Steam account, never has a character
        return accept(conn, c, AuthToken{}, nowSec);
    }

    if (h.token != AuthToken{}) {
        auto known = cfg_.store->tokenFor(h.steamId);
        if (!known || *known != h.token) return reject(conn, RejectReason::BadAuth);
        c.steamId = h.steamId;
        return accept(conn, c, AuthToken{}, nowSec);
    }

    // First visit on this server: prove the SteamID through Steam OpenID.
    c.nonce = toHex(cfg_.randomToken());
    pendingAuth_[c.nonce] = conn;
    return {Outgoing{conn, AuthChallenge{cfg_.authUrlBase + c.nonce}}};
}

std::vector<Outgoing> SessionManager::onAuthVerified(const std::string& nonce, uint64_t steamId, double nowSec) {
    auto p = pendingAuth_.find(nonce);
    if (p == pendingAuth_.end()) return {};
    ConnId conn = p->second;
    pendingAuth_.erase(p);
    auto it = conns_.find(conn);
    if (it == conns_.end() || it->second.player || !cfg_.store) return {};
    Conn& c = it->second;
    c.nonce.clear();
    if (cfg_.bannedSteamIds.contains(steamId)) return reject(conn, RejectReason::Banned);
    if (playerCount() >= cfg_.maxPlayers) return reject(conn, RejectReason::ServerFull);
    c.steamId = steamId;
    AuthToken token = cfg_.randomToken();
    cfg_.store->setToken(steamId, token);
    return accept(conn, c, token, nowSec);
}

std::vector<Outgoing> SessionManager::accept(ConnId conn, Conn& c, const AuthToken& newToken, double nowSec) {
    c.player = nextPlayerId_++;
    c.lastSaved = nowSec;
    std::optional<CharacterData> character;
    if (cfg_.store) character = cfg_.store->load(c.steamId);
    c.hasCharacter = character.has_value();

    HelloAck ack{RejectReason::None, *c.player};
    ack.hasCharacter = c.hasCharacter;
    ack.token = newToken;
    std::vector<Outgoing> out{Outgoing{conn, ack}};
    if (character) out.push_back({conn, *character});
    for (auto& [otherId, other] : conns_)
        if (otherId != conn && other.player && other.lastState) out.push_back({conn, *other.lastState});
    return out;
}

std::vector<Outgoing> SessionManager::handleCreate(ConnId conn, Conn& c, const CreateCharacter& m) {
    if (!cfg_.store || c.hasCharacter || m.name.empty() || m.race.empty()) return {};
    CharacterData d;
    d.name = m.name;
    d.race = m.race;
    d.appearance = m.appearance;
    d.x = cfg_.spawnX;
    d.y = cfg_.spawnY;
    d.z = cfg_.spawnZ;
    if (!cfg_.store->create(c.steamId, d)) return {};
    c.hasCharacter = true;
    return {Outgoing{conn, d}};
}

std::vector<Outgoing> SessionManager::handleState(ConnId conn, Conn& c, PlayerState s) {
    s.playerId = *c.player;  // never trust the id claimed by the client
    c.lastState = s;
    c.positionDirty = true;
    std::vector<Outgoing> out;
    for (auto& [otherId, other] : conns_)
        if (otherId != conn && other.player) out.push_back({otherId, s});
    return out;
}

void SessionManager::savePosition(Conn& c) {
    if (!cfg_.store || !c.hasCharacter || !c.lastState || !c.positionDirty) return;
    cfg_.store->savePosition(c.steamId, c.lastState->x, c.lastState->y, c.lastState->z, c.lastState->yaw);
    c.positionDirty = false;
}

std::vector<Outgoing> SessionManager::dropConn(ConnId conn) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return {};
    savePosition(it->second);
    if (!it->second.nonce.empty()) pendingAuth_.erase(it->second.nonce);
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
    for (auto& [id, c] : conns_) {
        if (nowSec - c.lastSeen > cfg_.timeoutSec) {
            expired.push_back(id);
        } else if (nowSec - c.lastSaved >= kPositionSaveIntervalSec) {
            savePosition(c);
            c.lastSaved = nowSec;
        }
    }
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
    return static_cast<size_t>(
        std::count_if(conns_.begin(), conns_.end(), [](auto& kv) { return kv.second.player.has_value(); }));
}

}  // namespace wok
