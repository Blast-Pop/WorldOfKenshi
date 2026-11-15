#include <wok/character_store.hpp>
#include <wok/session.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <utility>

namespace wok {

namespace {
// What the other players need to show this player's avatar.
template <typename C>
PlayerInfo infoOf(const C& c) {
    return PlayerInfo{*c.player, c.characterName, c.characterRace, c.characterTemplate, c.characterLook};
}
}  // namespace

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

SessionManager::SessionManager(SessionConfig cfg) : cfg_(std::move(cfg)) {
    if (!cfg_.store) return;
    for (NpcRecord& r : cfg_.store->loadNpcs()) {
        if (r.id >= nextNpcRecord_) nextNpcRecord_ = r.id + 1;
        const uint32_t id = r.id;
        npcRecords_[id] = {std::move(r), true, false};
    }
    if (!npcRecords_.empty()) std::printf("[world] %zu NPC record(s) loaded\n", npcRecords_.size());
}

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
    if (auto* chat = std::get_if<ChatSend>(&*msg)) return handleChat(conn, c, *chat, nowSec);
    if (auto* bind = std::get_if<NpcBind>(&*msg)) {
        handleNpcBind(conn, c, *bind);
        return {};
    }
    if (std::holds_alternative<NpcInfo>(*msg) || std::holds_alternative<NpcSnapshot>(*msg))
        return relayNpc(conn, c, *msg);
    if (std::holds_alternative<CharacterDied>(*msg)) {
        // Permanent death: the character is retired; the player stays connected and makes a new one.
        if (!c.hasCharacter || !cfg_.store) return {};
        cfg_.store->retire(c.steamId);
        c.hasCharacter = false;
        c.characterName.clear();
        c.characterLook.clear();
        c.positionDirty = c.statsDirty = c.inventoryDirty = false;
        c.inventory.clear();
        std::vector<Outgoing> out{Outgoing{conn, CharacterDied{}}};
        for (auto& [otherId, other] : conns_)
            if (otherId != conn && other.player) out.push_back({otherId, PlayerLeft{*c.player}});
        return out;
    }
    return {};  // server-to-client messages are ignored when received from a client
}

std::vector<Outgoing> SessionManager::reject(ConnId conn, RejectReason reason) {
    requestClose(conn);
    return {Outgoing{conn, HelloAck{reason, 0}}};
}

std::vector<Outgoing> SessionManager::handleHello(ConnId conn, Conn& c, const Hello& h, double nowSec) {
    if (h.protocolVersion != WOK_PROTOCOL_VERSION) return reject(conn, RejectReason::VersionMismatch);
    if (cfg_.requiredMods && h.mods != *cfg_.requiredMods) {  // spec 7.6: same mods, same order
        requestClose(conn);
        HelloAck ack{RejectReason::ModMismatch, 0};
        ack.serverMods = *cfg_.requiredMods;
        return {Outgoing{conn, ack}};
    }
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
    if (character) {
        c.characterRace = character->race;
        c.characterTemplate = character->templateId;
        c.characterLook = character->appearance;
    }
    for (auto& [otherId, other] : conns_) {
        if (otherId == conn || !other.player) continue;
        if (!other.characterName.empty()) out.push_back({conn, infoOf(other)});
        if (!c.characterName.empty()) out.push_back({otherId, infoOf(c)});
    }
    out.push_back({conn, cfg_.rules});
    if (cfg_.worldTime) out.push_back({conn, cfg_.worldTime(nowSec)});
    if (!cellOwners_.empty()) {  // shared world: who simulates what, for the newcomer (later only changes)
        CellOwners list;
        for (auto& [cell, owner] : cellOwners_) list.cells.push_back({cell.first, cell.second, owner});
        out.push_back({conn, list});
    }
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
    // Kenshi's own start choice decides where a new character begins, unless the server forces it.
    auto sane = [](float v) { return std::isfinite(v) && std::fabs(v) < 1.0e6f; };
    if (m.hasStart && !cfg_.forceSpawn && sane(m.startX) && sane(m.startY) && sane(m.startZ)) {
        d.x = m.startX;
        d.y = m.startY;
        d.z = m.startZ;
    }
    if (!cfg_.store->create(c.steamId, d)) return {};
    c.hasCharacter = true;
    c.characterName = d.name;
    c.characterRace = d.race;
    c.characterTemplate = d.templateId;
    c.characterLook = d.appearance;
    std::vector<Outgoing> out{Outgoing{conn, d}};
    for (auto& [otherId, other] : conns_)
        if (otherId != conn && other.player) out.push_back({otherId, infoOf(c)});
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
    if (nowSec >= nextCellUpdate_) {  // shared world: cell owners, once a second
        nextCellUpdate_ = nowSec + 1.0;
        auto owners = updateCellOwners();
        out.insert(out.end(), owners.begin(), owners.end());
    }
    if (cfg_.store && nowSec >= nextNpcSave_) {  // the world's memory, written in batches
        nextNpcSave_ = nowSec + kNpcSaveIntervalSec;
        std::vector<NpcRecord> changed;
        for (auto& [id, m] : npcRecords_)
            if (m.dirty && m.placed) {
                changed.push_back(m.rec);
                m.dirty = false;
            }
        cfg_.store->saveNpcs(changed);
    }
    now_ = nowSec;
    if (nowSec >= nextNpcForget_) {  // offered to an owner who has no such NPC: forget it
        nextNpcForget_ = nowSec + kNpcSaveIntervalSec;
        std::unordered_set<uint32_t> bound;
        for (auto& [id, c] : conns_)
            for (auto& [key, record] : c.npcKeys) bound.insert(record);
        std::vector<uint32_t> forgotten;
        for (auto it = npcRecords_.begin(); it != npcRecords_.end();) {
            NpcMemory& m = it->second;
            if (m.offered == 0 || nowSec - m.offered < kNpcOfferSec) {
                ++it;
            } else if (bound.count(it->first) || m.rec.dead) {  // continued, or dead until it is forgotten
                m.offered = 0;
                ++it;
            } else {
                forgotten.push_back(it->first);
                it = npcRecords_.erase(it);
            }
        }
        if (cfg_.store) cfg_.store->forgetNpcs(forgotten);
    }
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

std::vector<Outgoing> SessionManager::handleChat(ConnId conn, Conn& c, const ChatSend& m, double nowSec) {
    if (!c.hasCharacter && c.characterName.empty()) return {};
    if (nowSec < c.nextChat) return {};  // anti-spam: at most two messages a second
    c.nextChat = nowSec + 0.5;
    std::string text;
    for (char ch : m.text)  // printable only (UTF-8 bytes kept), no control characters
        if (static_cast<unsigned char>(ch) >= 0x20) text += ch;
    while (!text.empty() && text.front() == ' ') text.erase(text.begin());
    while (!text.empty() && text.back() == ' ') text.pop_back();
    if (text.empty()) return {};
    std::printf("[chat %s] %s: %s\n", m.channel == ChatChannel::Local ? "local" : "global", c.characterName.c_str(),
                text.c_str());
    std::fflush(stdout);
    ChatMessage out{m.channel, *c.player, c.characterName, text};
    std::vector<Outgoing> sent;
    for (auto& [otherId, other] : conns_) {
        if (!other.player) continue;
        if (m.channel == ChatChannel::Local && otherId != conn) {
            if (!c.lastState || !other.lastState) continue;
            const float dx = other.lastState->x - c.lastState->x, dz = other.lastState->z - c.lastState->z;
            if (dx * dx + dz * dz > cfg_.chatLocalRadius * cfg_.chatLocalRadius) continue;
        }
        sent.push_back({otherId, out});
    }
    return sent;
}

std::vector<Outgoing> SessionManager::updateCellOwners() {
    // Every player in play claims its cell and the 8 around it (what it can see); among the players
    // claiming a cell the earliest (lowest id) owns it, and an owner keeps a cell while it still claims it.
    std::map<std::pair<int32_t, int32_t>, std::vector<PlayerId>> present;
    for (auto& [id, c] : conns_)
        if (c.player && c.lastState && (c.hasCharacter || !c.characterName.empty())) {
            const int32_t cx = cellOf(c.lastState->x), cz = cellOf(c.lastState->z);
            for (int32_t dx = -1; dx <= 1; ++dx)
                for (int32_t dz = -1; dz <= 1; ++dz) present[{cx + dx, cz + dz}].push_back(*c.player);
        }
    std::map<std::pair<int32_t, int32_t>, PlayerId> next;
    for (auto& [cell, players] : present) {
        auto old = cellOwners_.find(cell);
        if (old != cellOwners_.end() && std::find(players.begin(), players.end(), old->second) != players.end())
            next[cell] = old->second;
        else
            next[cell] = *std::min_element(players.begin(), players.end());
    }
    if (next == cellOwners_) return {};
    std::map<PlayerId, std::vector<std::pair<int32_t, int32_t>>> gained;
    for (auto& [cell, owner] : next)
        if (auto old = cellOwners_.find(cell); old == cellOwners_.end() || old->second != owner)
            gained[owner].push_back(cell);
    cellOwners_ = std::move(next);
    CellOwners list;
    for (auto& [cell, owner] : cellOwners_) list.cells.push_back({cell.first, cell.second, owner});
    std::vector<Outgoing> out;
    // A new owner first gets what the world remembers of its cells, then the list that makes it owner.
    for (auto& [id, c] : conns_)
        if (c.player)
            if (auto g = gained.find(*c.player); g != gained.end()) {
                auto records = recordsFor(id, g->second);
                out.insert(out.end(), records.begin(), records.end());
            }
    for (auto& [id, c] : conns_)
        if (c.player) out.push_back({id, list});
    return out;
}

std::vector<Outgoing> SessionManager::recordsFor(ConnId conn, const std::vector<std::pair<int32_t, int32_t>>& gained) {
    std::unordered_set<uint32_t> own;  // already followed by this player
    for (auto& [key, record] : conns_[conn].npcKeys) own.insert(record);
    std::unordered_set<uint32_t> given;
    std::vector<Outgoing> out;
    NpcRecords batch;
    for (auto& [id, m] : npcRecords_) {
        if (!m.placed || own.count(id)) continue;
        const std::pair<int32_t, int32_t> cell{cellOf(m.rec.x), cellOf(m.rec.z)};
        if (std::find(gained.begin(), gained.end(), cell) == gained.end()) continue;
        given.insert(id);
        if (const auto& at = conns_[conn].lastState) {
            const float dx = m.rec.x - at->x, dz = m.rec.z - at->z;
            if (dx * dx + dz * dz <= kNpcOfferRadius * kNpcOfferRadius) m.offered = now_ > 0 ? now_ : 1e-9;
        }
        batch.records.push_back(m.rec);
        if (batch.records.size() == kMaxNpcRecordsPerMessage) {
            out.push_back({conn, batch});
            batch.records.clear();
        }
    }
    if (!batch.records.empty()) out.push_back({conn, batch});
    // Whoever followed those NPCs before no longer speaks for them.
    for (auto& [otherId, other] : conns_)
        if (otherId != conn) std::erase_if(other.npcKeys, [&](const auto& kv) { return given.count(kv.second) != 0; });
    return out;
}

void SessionManager::handleNpcBind(ConnId conn, Conn& c, const NpcBind& m) {
    for (const NpcBinding& b : m.bindings) {
        if (!npcRecords_.count(b.record)) continue;
        bool taken = false;
        for (auto& [otherId, other] : conns_)
            if (otherId != conn)
                for (auto& [key, record] : other.npcKeys)
                    if (record == b.record) taken = true;
        if (!taken) c.npcKeys[b.key] = b.record;
    }
}

void SessionManager::rememberNpc(Conn& c, const Message& m) {
    if (auto* info = std::get_if<NpcInfo>(&m)) {
        if (c.npcKeys.count(info->key) || npcRecords_.size() >= kMaxNpcRecords) return;
        NpcMemory mem;  // an NPC the world did not know yet
        mem.rec.id = nextNpcRecord_++;
        mem.rec.name = info->name;
        mem.rec.templateId = info->templateId;
        c.npcKeys[info->key] = mem.rec.id;
        npcRecords_[mem.rec.id] = std::move(mem);
    } else if (auto* snap = std::get_if<NpcSnapshot>(&m)) {
        for (const NpcState& s : snap->npcs) {
            auto key = c.npcKeys.find(s.key);
            if (key == c.npcKeys.end()) continue;
            auto rec = npcRecords_.find(key->second);
            if (rec == npcRecords_.end()) continue;
            NpcMemory& mem = rec->second;
            const float dx = s.x - mem.rec.x, dy = s.y - mem.rec.y, dz = s.z - mem.rec.z;
            if (mem.placed && mem.rec.dead == s.dead && dx * dx + dy * dy + dz * dz < 1.0f) continue;
            mem.rec.x = s.x;
            mem.rec.y = s.y;
            mem.rec.z = s.z;
            mem.rec.dead = s.dead;
            mem.placed = mem.dirty = true;
        }
    }
}

std::vector<Outgoing> SessionManager::relayNpc(ConnId conn, Conn& c, Message m) {
    // Only a cell owner speaks for NPCs, and only to the players around it.
    bool owner = false;
    for (auto& [cell, who] : cellOwners_)
        if (who == *c.player) owner = true;
    if (!owner || !c.lastState) return {};
    rememberNpc(c, m);
    if (auto* info = std::get_if<NpcInfo>(&m)) info->owner = *c.player;
    if (auto* snap = std::get_if<NpcSnapshot>(&m)) snap->owner = *c.player;
    constexpr float kRelayRadius = 2.0f * kCellSize;
    std::vector<Outgoing> out;
    for (auto& [otherId, other] : conns_) {
        if (otherId == conn || !other.player || !other.lastState) continue;
        const float dx = other.lastState->x - c.lastState->x, dz = other.lastState->z - c.lastState->z;
        if (dx * dx + dz * dz <= kRelayRadius * kRelayRadius) out.push_back({otherId, m});
    }
    return out;
}

}  // namespace wok
