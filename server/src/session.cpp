#include <wok/character_store.hpp>
#include <wok/session.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <utility>

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
        c.helloName = std::string(h->name, strnlen(h->name, sizeof(h->name)));
        return handleHello(conn, c, *h, nowSec);
    }
    if (std::holds_alternative<ServerInfoRequest>(*msg)) {  // server browser query, no login needed
        ServerInfo info;
        info.name = cfg_.serverName.substr(0, kMaxServerNameBytes);
        info.description = cfg_.serverDescription.substr(0, kMaxServerDescriptionBytes);
        info.players = static_cast<uint16_t>(playerCount());
        info.maxPlayers = static_cast<uint16_t>(cfg_.maxPlayers);
        return {Outgoing{conn, info}};
    }
    if (!c.player) return {};  // everything else requires an accepted player
    if (auto* s = std::get_if<PlayerState>(&*msg)) return handleState(conn, c, *s);
    if (auto* cc = std::get_if<CreateCharacter>(&*msg)) return handleCreate(conn, c, *cc);
    if (auto* iu = std::get_if<InventoryUpdate>(&*msg)) {
        if (c.hasCharacter) {
            c.inventory = iu->items;
            c.inventoryDirty = true;
        }
        return {};
    }
    if (auto* st = std::get_if<StatsUpdate>(&*msg)) {
        handleStats(c, *st);
        return {};
    }
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

    // Steam session ticket: Steam itself vouches for the account (and its Kenshi licence).
    if (!h.ticket.empty() && cfg_.beginSteamAuth && cfg_.steamLicense && h.steamId >= kMaxTestBotSteamId &&
        cfg_.beginSteamAuth(h.steamId, h.ticket)) {
        c.steamPending = h.steamId;
        c.steamPendingSince = nowSec;
        return {};  // answered from tick() once Steam has checked it
    }

    if (h.token != AuthToken{}) {
        auto known = cfg_.store->tokenFor(h.steamId);
        if (known && *known == h.token) {
            c.steamId = h.steamId;
            return accept(conn, c, AuthToken{}, nowSec);
        }
        // Stale or unknown token (e.g. this server's database was reset): fall back to Steam,
        // which proves the account and issues a fresh token.
    }

    // First visit on this server (or no valid token): prove the SteamID through Steam OpenID.
    // Reuse a Steam login already in progress for this claimed SteamID (player clicked Connect again).
    if (auto byId = pendingByClaimed_.find(h.steamId); byId != pendingByClaimed_.end()) {
        auto p = pendingAuth_.find(byId->second);
        if (p != pendingAuth_.end() && nowSec - p->second.created <= kPendingAuthTtlSec) {
            if (p->second.verifiedSteamId) {  // Steam already confirmed while the player was away
                uint64_t id = p->second.verifiedSteamId;
                pendingByClaimed_.erase(byId);
                pendingAuth_.erase(p);
                return completeAuth(conn, c, id, nowSec);
            }
            c.nonce = p->first;
            p->second.conn = conn;
            return {Outgoing{conn, AuthChallenge{cfg_.authUrlBase + c.nonce}}};
        }
    }
    c.nonce = toHex(cfg_.randomToken());
    pendingAuth_[c.nonce] = PendingAuth{conn, h.steamId, nowSec, 0};
    pendingByClaimed_[h.steamId] = c.nonce;
    return {Outgoing{conn, AuthChallenge{cfg_.authUrlBase + c.nonce}}};
}

std::vector<Outgoing> SessionManager::onAuthVerified(const std::string& nonce, uint64_t steamId, double nowSec) {
    auto p = pendingAuth_.find(nonce);
    if (p == pendingAuth_.end() || !cfg_.store) return {};
    auto it = p->second.conn ? conns_.find(p->second.conn) : conns_.end();
    if (it == conns_.end() || it->second.player) {
        p->second.verifiedSteamId = steamId;  // player away: finish on their next Hello
        p->second.conn = 0;
        return {};
    }
    pendingByClaimed_.erase(p->second.claimedSteamId);
    pendingAuth_.erase(p);
    it->second.nonce.clear();
    return completeAuth(it->first, it->second, steamId, nowSec);
}

std::vector<Outgoing> SessionManager::completeAuth(ConnId conn, Conn& c, uint64_t steamId, double nowSec) {
    c.nonce.clear();
    if (cfg_.bannedSteamIds.contains(steamId)) return reject(conn, RejectReason::Banned);
    if (playerCount() >= cfg_.maxPlayers) return reject(conn, RejectReason::ServerFull);
    c.steamId = steamId;
    AuthToken token = cfg_.randomToken();
    cfg_.store->setToken(steamId, token);
    return accept(conn, c, token, nowSec);
}

std::vector<Outgoing> SessionManager::accept(ConnId conn, Conn& c, const AuthToken& newToken, double nowSec) {
    // One connection per account: a new login closes the previous ones (saving their position first).
    std::vector<Outgoing> kicked;
    if (c.steamId != 0) {
        std::vector<ConnId> previous;
        for (auto& [otherId, other] : conns_)
            if (otherId != conn && other.player && other.steamId == c.steamId) previous.push_back(otherId);
        for (ConnId old : previous) {
            auto left = dropConn(old);
            kicked.insert(kicked.end(), left.begin(), left.end());
            requestClose(old);
        }
    }
    c.player = nextPlayerId_++;
    c.lastSaved = nowSec;
    std::optional<CharacterData> character;
    if (cfg_.store) character = cfg_.store->load(c.steamId);
    c.hasCharacter = character.has_value();
    if (character)
        for (auto& st : character->stats) c.stats[st.id] = st.value;

    HelloAck ack{RejectReason::None, *c.player};
    ack.hasCharacter = c.hasCharacter;
    ack.token = newToken;
    std::vector<Outgoing> out{Outgoing{conn, ack}};
    if (character) out.push_back({conn, *character});
    // Names: the newcomer learns the others', the others learn the newcomer's.
    c.characterName = character ? character->name : c.helloName;
    for (auto& [otherId, other] : conns_) {
        if (otherId == conn || !other.player) continue;
        if (!other.characterName.empty()) out.push_back({conn, PlayerInfo{*other.player, other.characterName}});
        if (!c.characterName.empty()) out.push_back({otherId, PlayerInfo{*c.player, c.characterName}});
    }
    if (cfg_.worldTime) out.push_back({conn, cfg_.worldTime(nowSec)});
    for (auto& [otherId, other] : conns_)
        if (otherId != conn && other.player && other.lastState) out.push_back({conn, *other.lastState});
    out.insert(out.end(), kicked.begin(), kicked.end());
    return out;
}

std::vector<Outgoing> SessionManager::handleCreate(ConnId conn, Conn& c, const CreateCharacter& m) {
    if (!cfg_.store || c.hasCharacter || m.name.empty() || m.race.empty()) return {};
    CharacterData d;
    d.name = m.name;
    d.race = m.race;
    d.appearance = m.appearance;
    d.templateId = m.templateId;
    d.x = cfg_.spawnX;
    d.y = cfg_.spawnY;
    d.z = cfg_.spawnZ;
    if (!cfg_.spawnPoints.empty()) {
        size_t n = cfg_.spawnPoints.size();
        size_t i = cfg_.pickSpawn ? cfg_.pickSpawn(n) % n : static_cast<size_t>(std::rand()) % n;
        const SpawnPoint& sp = cfg_.spawnPoints[i];
        d.x = sp.x;
        d.y = sp.y;
        d.z = sp.z;
    }
    if (!cfg_.store->create(c.steamId, d)) return {};
    c.hasCharacter = true;
    c.characterName = d.name;
    std::vector<Outgoing> out{Outgoing{conn, d}};
    for (auto& [otherId, other] : conns_)
        if (otherId != conn && other.player) out.push_back({otherId, PlayerInfo{*c.player, d.name}});
    return out;
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

void SessionManager::handleStats(Conn& c, const StatsUpdate& m) {
    if (!c.hasCharacter) return;
    for (auto& st : m.values) c.stats[st.id] = st.value;  // ids and values already validated by decode
    c.statsDirty = !m.values.empty() || c.statsDirty;
}

void SessionManager::savePosition(Conn& c) {
    if (!cfg_.store || !c.hasCharacter) return;
    if (c.lastState && c.positionDirty) {
        cfg_.store->savePosition(c.steamId, c.lastState->x, c.lastState->y, c.lastState->z, c.lastState->yaw);
        c.positionDirty = false;
    }
    if (c.inventoryDirty) {
        cfg_.store->saveInventory(c.steamId, c.inventory);
        c.inventoryDirty = false;
    }
    if (c.statsDirty) {
        std::vector<StatValue> all;
        for (auto& [id, value] : c.stats) all.push_back({id, value});
        cfg_.store->saveStats(c.steamId, all);
        c.statsDirty = false;
    }
}

std::vector<Outgoing> SessionManager::dropConn(ConnId conn) {
    auto it = conns_.find(conn);
    if (it == conns_.end()) return {};
    savePosition(it->second);
    if (cfg_.endSteamAuth && it->second.player && it->second.steamId >= kMaxTestBotSteamId)
        cfg_.endSteamAuth(it->second.steamId);
    if (auto p = pendingAuth_.find(it->second.nonce); p != pendingAuth_.end() && p->second.conn == conn)
        p->second.conn = 0;  // keep the Steam login alive for a reconnect
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
        } else if (nowSec - c.lastSaved >= kProgressSaveIntervalSec) {
            savePosition(c);
            c.lastSaved = nowSec;
        }
    }
    for (auto p = pendingAuth_.begin(); p != pendingAuth_.end();) {
        if (nowSec - p->second.created > kPendingAuthTtlSec) {
            pendingByClaimed_.erase(p->second.claimedSteamId);
            p = pendingAuth_.erase(p);
        } else {
            ++p;
        }
    }
    std::vector<Outgoing> out;
    for (auto& [id, c] : conns_) {
        if (!c.steamPending || c.player) continue;
        uint64_t steamId = c.steamPending;
        int licence = cfg_.steamLicense(steamId);
        if (licence == 0) {
            c.steamPending = 0;
            std::printf("[steam] %llu verified by Steam (owns Kenshi)\n", static_cast<unsigned long long>(steamId));
            auto acc = completeAuth(id, c, steamId, nowSec);
            out.insert(out.end(), acc.begin(), acc.end());
        } else if (licence == 1) {
            c.steamPending = 0;
            if (cfg_.endSteamAuth) cfg_.endSteamAuth(steamId);
            auto rej = reject(id, RejectReason::BadAuth);  // this account does not own Kenshi
            out.insert(out.end(), rej.begin(), rej.end());
        } else if (nowSec - c.steamPendingSince > kSteamAuthTimeoutSec) {
            c.steamPending = 0;  // Steam too slow: fall back to the browser login
            if (cfg_.endSteamAuth) cfg_.endSteamAuth(steamId);
            c.nonce = toHex(cfg_.randomToken());
            pendingAuth_[c.nonce] = PendingAuth{id, steamId, nowSec, 0};
            pendingByClaimed_[steamId] = c.nonce;
            out.push_back({id, AuthChallenge{cfg_.authUrlBase + c.nonce}});
        }
    }
    if (cfg_.worldTime && nowSec - lastWorldTimeSent_ >= kWorldTimeIntervalSec) {
        lastWorldTimeSent_ = nowSec;
        WorldTime t = cfg_.worldTime(nowSec);
        for (auto& [id, c] : conns_)
            if (c.player) out.push_back({id, t});
    }
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
