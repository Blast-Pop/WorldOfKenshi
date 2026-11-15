// Entry points called by OGRE: Kenshi loads every "Plugin=" listed in Plugins_x64.cfg and calls
// dllStartPlugin() during Ogre::Root initialisation, dllStopPlugin() at shutdown.
// Nothing here may throw into the game or touch OGRE objects beyond registering hooks.

#include "frame_hook.hpp"
#include "game/avatars.hpp"
#include "game/game_memory.hpp"
#include "game/direct_control.hpp"
#include "game/world_characters.hpp"
#include "game/shared_npcs.hpp"
#include "ui/chat.hpp"
#include "ui/options_tab.hpp"
#include "ui/name_tags.hpp"
#include "game/identity.hpp"
#include "game/inventory.hpp"
#include "game/items.hpp"
#include "game/stats.hpp"
#include "game/world_time.hpp"
#include "game/local_player.hpp"
#include "game/re_probe.hpp"
#include "input.hpp"
#include "log.hpp"
#include "net_client.hpp"
#include "steam_identity.hpp"
#include "ui/multiplayer_menu.hpp"
#include "ui/mygui_abi.hpp"

#include <core/appearance.hpp>

#include <core/config.hpp>
#include <core/motion.hpp>
#include <core/paths.hpp>
#include <core/tokens.hpp>

#include <wok/protocol.hpp>

#include <cstdio>
#include <cmath>
#include <fstream>
#include <algorithm>
#include <map>
#include <set>
#include <unordered_set>
#include <memory>
#include <sstream>

namespace {

wok::ClientConfig loadOrCreateConfig() {
    const std::string path = wok::modFile(wok::modDir(), "wok_client.ini");
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        wok::ClientConfig defaults;
        std::ofstream(path, std::ios::binary) << wok::serializeConfig(defaults);
        wok::log("created default config %s", path.c_str());
        return defaults;
    }
    std::stringstream ss;
    ss << in.rdbuf();
    return wok::parseConfig(ss.str());
}

struct ClientState {
    wok::ClientConfig cfg;
    wok::LocalPlayerReader reader;
    bool readerOk = false;
    wok::ClientNet* net = new wok::ClientNet;  // intentionally leaked: never torn down during process exit
    wok::SendThrottle throttle{10.0};
    double now = 0.0;
    uint32_t tick = 0;
    double fpsTimer = 0.0;
    int frames = 0;
    std::map<wok::PlayerId, double> lastRemoteLog;
    wok::MultiplayerMenu* menu = nullptr;
    wok::AvatarManager avatars;
    bool avatarsOk = false;
    bool wasConnected = false;
    uint64_t steamId = 0;
    bool characterSent = false;  // CreateCharacter already sent during this connection
    bool characterRestored = false;
    std::map<uint8_t, float> statsSent;  // skills the server already has
    bool dead = false;                   // our character died; reported, waiting for the server
    double nextDeathCheck = 0;
    double nextCreateDiag = 0;
    wok::DirectControl control;          // WASD movement
    wok::SharedNpcs shared;              // shared world: NPCs (spec 7.3)
    bool sharedOk = false;
    std::unique_ptr<wok::Chat> chat;     // in-game chat (spec 7.8)
    wok::MultiplayerOptions options;     // our tab in Kenshi's Options
    double nextOptionsPoll = 0;
    float hungerKept = 0;               // hunger before the last clock move
    double hungerGuardUntil = 0;        // until then, the clock move must not make us hungry
    uintptr_t deadCharacter = 0;         // the retired body, still in the world until New Game
    wok::Items items;
    bool itemsOk = false;
    std::string inventorySent;           // inventory text the server already has
    double nextInventoryPoll = 0;
    double nextStatsPoll = 0;  // returning player's server character recreated in this world
};

std::string tokensPath() { return wok::modFile(wok::modDir(), "tokens.ini"); }

wok::TokenFile loadTokens() {
    std::ifstream in(tokensPath(), std::ios::binary);
    std::stringstream ss;
    ss << in.rdbuf();
    return wok::parseTokens(ss.str());
}

void saveConfig(const wok::ClientConfig& cfg) {
    std::ofstream(wok::modFile(wok::modDir(), "wok_client.ini"), std::ios::binary) << wok::serializeConfig(cfg);
}

ClientState* g_state = nullptr;

// Connects to cfg.host:port with our SteamID and the token this server gave us earlier (if any).
void connectToServer() {
    auto& s = *g_state;
    if (!s.steamId) s.steamId = wok::localSteamId();
    auto tokens = loadTokens();
    auto it = tokens.find(wok::serverKey(s.cfg.host, s.cfg.port));
    wok::cancelSteamAuthTicket();  // one live ticket at a time
    auto ticket = wok::steamAuthTicket();  // lets a Steam-enabled server skip the browser login
    s.net->start(s.cfg, s.steamId, it == tokens.end() ? wok::AuthToken{} : it->second,
                 std::string(ticket.begin(), ticket.end()));
}

// Per-frame cost of our code, by part (dev measure, logged every 10 s with the frame rate).
struct FrameProfile {
    enum Part { Probe, Menu, Send, Polls, Incoming, Avatars, Count };
    double ms[Count] = {};
    double worstFrameMs = 0;
    static double nowMs() {
        static LARGE_INTEGER f = [] { LARGE_INTEGER x; QueryPerformanceFrequency(&x); return x; }();
        LARGE_INTEGER c;
        QueryPerformanceCounter(&c);
        return 1000.0 * static_cast<double>(c.QuadPart) / static_cast<double>(f.QuadPart);
    }
};
FrameProfile g_prof;
double g_lap = 0;
void lap(FrameProfile::Part part) {
    double t = FrameProfile::nowMs();
    g_prof.ms[part] += t - g_lap;
    g_lap = t;
}

bool devSwitch(const char* name) {  // dev-only switches: an empty file in WorldOfKenshi/
    bool on = std::ifstream(wok::modFile(wok::modDir(), name)).good();
    if (on) wok::log("dev switch on: %s", name);
    return on;
}

void onFrame(float dt) {
    auto& s = *g_state;
    s.now += dt;
    const double frameStart = FrameProfile::nowMs();
    g_lap = frameStart;
    wok::probeTick();
    lap(FrameProfile::Probe);
    if (s.menu) s.menu->update(dt);
    lap(FrameProfile::Menu);

    if (auto issued = s.net->takeIssuedToken()) {  // first visit verified by Steam: keep our secret
        auto tokens = loadTokens();
        tokens[wok::serverKey(s.cfg.host, s.cfg.port)] = *issued;
        std::ofstream(tokensPath(), std::ios::binary) << wok::serializeTokens(tokens);
        wok::log("auth: token saved for %s:%u", s.cfg.host.c_str(), s.cfg.port);
    }

    if (wok::keyPressed(VK_F9)) {
        auto st = s.net->status();
        if (st == wok::ClientNet::Status::Connected || st == wok::ClientNet::Status::Connecting)
            wok::log("F9: already %s", st == wok::ClientNet::Status::Connected ? "connected" : "connecting");
        else
            connectToServer();
    }

    if (s.readerOk && s.net->status() == wok::ClientNet::Status::Connected && s.throttle.due(s.now)) {
        if (auto snap = s.reader.read(s.now)) {
            wok::PlayerState ps{};
            ps.tick = ++s.tick;
            ps.x = snap->x;
            ps.y = snap->y;
            ps.z = snap->z;
            ps.yaw = snap->yaw;
            ps.anim = snap->anim;
            s.net->pushLocal(ps);
        }
    }

    bool connected = s.net->status() == wok::ClientNet::Status::Connected;
    if (s.wasConnected && !connected) {
        s.avatars.clear();
        if (s.sharedOk) s.shared.clear();
        wok::cancelSteamAuthTicket();  // a ticket stays valid until cancelled: no replay after we leave
    }
    if (!connected) {
        s.characterSent = false;
        s.characterRestored = false;
        s.dead = false;
        s.deadCharacter = 0;
    }

    // Returning player: Kenshi's New Game only loads the world; bring back the server character
    // (template, name) into the player's squad at its saved position.
    if (connected && !s.characterRestored && s.net->hasCharacter() && s.avatarsOk && s.reader.character()) {
        if (auto data = s.net->character()) {
            s.characterRestored = true;
            s.avatars.restorePlayer(s.reader.character(), *data);
            int applied = wok::writeStats(s.reader.character(), data->stats);
            wok::log("stats: %d saved skills applied", applied);
            for (auto& st : data->stats) s.statsSent[st.id] = st.value;
            if (!s.itemsOk) s.itemsOk = s.items.init(s.reader.gameWorld());
            std::string savedInventory(data->inventory.begin(), data->inventory.end());
            if (s.itemsOk && !savedInventory.empty())
                wok::restoreInventory(s.reader.character(), savedInventory, s.items);
            s.inventorySent = wok::readInventory(s.reader.character());
            // Check the look really came back (Kenshi's editor Import does the work).
            if (auto now = wok::readIdentity(s.reader.character())) {
                auto saved = wok::decodeAppearance(data->appearance);
                auto got = wok::decodeAppearance(now->appearance);
                for (const char* key : {"gender", "hair", "beard"})
                    wok::log("look check: %s saved '%s' in game '%s'%s", key, saved[key].c_str(), got[key].c_str(),
                             saved[key] == got[key] ? "" : "  <-- DIFFERENT");
                wok::log("look check: race saved '%s' in game '%s'", data->race.c_str(), now->race.c_str());
            }
            if (s.menu) s.menu->setWorldReady();  // lift the loading screen
        }
    }
    // Continuous save (spec 7.7): once a second, send the skills that changed since last time.
    lap(FrameProfile::Send);
    // Permanent death (server rule): report it once; the server retires the character.
    if (connected && !s.dead && s.net->character() && (s.characterRestored || s.characterSent) &&
        s.now >= s.nextDeathCheck) {
        s.nextDeathCheck = s.now + 1.0;
        if (wok::isDead(s.reader.character())) {
            s.dead = true;
            s.deadCharacter = s.reader.character();  // never re-register this body
            s.net->reportDeath();
            wok::log("character: died, reported to the server (permanent death)");
        }
    }

    if (connected && !s.dead && s.net->character() && (s.characterRestored || s.characterSent) && s.now >= s.nextStatsPoll) {
        s.nextStatsPoll = s.now + 1.0;
        wok::StatsUpdate u;
        for (auto& st : wok::readStats(s.reader.character())) {
            auto it = s.statsSent.find(st.id);
            if (it != s.statsSent.end() && std::fabs(it->second - st.value) < 1e-4f) continue;
            s.statsSent[st.id] = st.value;
            u.values.push_back(st);
        }
        if (!u.values.empty()) {
            s.net->sendStats(u);
            wok::log("stats: %zu changed skill(s) sent", u.values.size());
        }
    }
    if (!connected) s.statsSent.clear();
    // Same for the inventory, as a whole, every 2 s when it changed.
    if (connected && !s.dead && s.net->character() && (s.characterRestored || s.characterSent) && s.now >= s.nextInventoryPoll) {
        s.nextInventoryPoll = s.now + 2.0;
        std::string inv = wok::readInventory(s.reader.character());
        if (inv != s.inventorySent && inv.size() <= wok::kMaxInventoryBytes) {
            s.inventorySent = inv;
            s.net->sendInventory(std::vector<uint8_t>(inv.begin(), inv.end()));
            wok::log("inventory: sent (%zu bytes)", inv.size());
        }
    }
    if (!connected) s.inventorySent.clear();

    // First visit: once the player has made their character with Kenshi's own New Game flow,
    // register it on the server (name, race and look by FCS string ID).
    if (connected && !s.characterSent && !s.net->hasCharacter() && s.reader.character() &&
        s.reader.character() != s.deadCharacter) {
        if (auto c = wok::readIdentity(s.reader.character())) {
            if (auto pos = s.reader.read(s.now)) {  // where Kenshi's chosen start put the character
                c->hasStart = true;
                c->startX = pos->x;
                c->startY = pos->y;
                c->startZ = pos->z;
            }
            s.net->createCharacter(*c);
            if (s.menu) s.menu->newCharacterSent();
            s.characterSent = true;
            s.statsSent.clear();  // the first stats poll sends every skill of the new character
            wok::log("character: sent '%s' (%s, look %zu bytes) to the server", c->name.c_str(), c->race.c_str(), c->appearance.size());
        }
    }
    // Diagnostic: a new player whose character is not registered yet, every 5 s.
    if (connected && !s.characterSent && !s.net->hasCharacter() && s.now >= s.nextCreateDiag) {
        s.nextCreateDiag = s.now + 5.0;
        uintptr_t ch = s.reader.character();
        bool human = ch && wok::mem::isA(ch, ".?AVCharacterHuman@@");
        auto id = ch ? wok::readIdentity(ch) : std::nullopt;
        wok::log("character: not sent yet (char %p, human %d, dead body %d, identity %d '%s' '%s')",
                 reinterpret_cast<void*>(ch), human ? 1 : 0, ch && ch == s.deadCharacter ? 1 : 0, id ? 1 : 0,
                 id ? id->name.c_str() : "", id ? id->race.c_str() : "");
    }
    s.wasConnected = connected;
    lap(FrameProfile::Polls);
    for (auto& m : s.net->drainIncoming()) {
        // Only a death we saw in game opens the death window (not a deletion from the menu).
        if (std::holds_alternative<wok::CharacterDied>(m) && !s.dead) continue;
        if (std::holds_alternative<wok::CharacterDied>(m)) {  // retired: make a new one, still connected
            s.dead = false;
            s.characterSent = false;
            s.characterRestored = false;
            s.statsSent.clear();
            if (s.menu) s.menu->showDeathWindow();
            continue;
        }
        if (auto* ps = std::get_if<wok::PlayerState>(&m)) {
            if (s.now - s.lastRemoteLog[ps->playerId] >= 5.0) {  // light trace, 1 line / 5 s / player
                s.lastRemoteLog[ps->playerId] = s.now;
                wok::log("remote player %u at (%.1f, %.1f, %.1f) anim %u", ps->playerId, ps->x, ps->y, ps->z,
                         static_cast<unsigned>(ps->anim));
            }
        }
        // The server owns the world clock: align ours once the world is being played.
        if (std::holds_alternative<wok::CellOwners>(m) || std::holds_alternative<wok::NpcInfo>(m) ||
            std::holds_alternative<wok::NpcSnapshot>(m)) {
            if (s.sharedOk) s.shared.onMessage(m, s.now);
            continue;
        }
        if (auto* cm = std::get_if<wok::ChatMessage>(&m)) {
            if (s.chat) s.chat->onMessage(*cm, s.now);
            continue;
        }
        if (auto* wt = std::get_if<wok::WorldTime>(&m); wt && s.characterRestored)
        {
            if (s.menu) s.menu->setServerDay(wt->day);
            static const bool noClock = devSwitch("dev_no_clock");
            if (noClock) continue;
            float hunger = 0;
            bool known = wok::readHunger(s.reader.character(), hunger);
            if (wok::applyWorldTime(s.reader.gameWorld(), *wt) && known) {
                s.hungerKept = hunger;  // the hours skipped are not time lived by our character
                s.hungerGuardUntil = s.now + 5.0;
            }
        }
        if (s.avatarsOk) s.avatars.onMessage(m, s.now);
    }
    if (s.now < s.hungerGuardUntil) {
        float hunger = 0;
        if (wok::readHunger(s.reader.character(), hunger) && hunger < s.hungerKept)
            wok::writeHunger(s.reader.character(), s.hungerKept);
    }
    lap(FrameProfile::Incoming);
    if (s.avatarsOk) s.avatars.update(s.now);
    // RE probe Ctrl+Shift+F4: snapshot Kenshi's camera object and PlayerInterface, diff on the next press.
    {
        static bool wasDown = false;
        static std::vector<uint8_t> before[2];
        bool down = (GetAsyncKeyState(VK_F4) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000);
        if (down && !wasDown) {
            const uintptr_t gw = wok::mem::moduleBase() + 0x2134110;
            uintptr_t objs[2] = {};
            wok::mem::readPtr(gw + 0x10, objs[0]);
            wok::mem::readPtr(gw + 0x580, objs[1]);
            for (int k = 0; k < 2; ++k) {
                std::vector<uint8_t> now(0x800);
                if (!objs[k] || !wok::mem::readBytes(objs[k], now.data(), now.size())) continue;
                if (before[k].empty()) {
                    before[k] = now;
                    wok::log("probe: %s snapshot taken", k ? "PlayerInterface" : "camera");
                    continue;
                }
                for (size_t o = 0; o < now.size(); o += 8)
                    if (std::memcmp(&now[o], &before[k][o], 8) != 0) {
                        uint64_t a = 0, b = 0;
                        std::memcpy(&a, &before[k][o], 8);
                        std::memcpy(&b, &now[o], 8);
                        wok::log("probe: %s +0x%zx: %016llx -> %016llx", k ? "PlayerInterface" : "camera", o,
                                 static_cast<unsigned long long>(a), static_cast<unsigned long long>(b));
                    }
                before[k].clear();
            }
        }
        wasDown = down;
    }
    // RE probe Ctrl+Shift+F1: which containers hold the characters near the player? Breadth-first over
    // pointers from GameWorld (3 levels); every object pointing at characters within 1500 units is
    // counted, the best ones logged with their path.
    {
        static bool wasDown = false;
        bool down = (GetAsyncKeyState(VK_F1) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000);
        if (down && !wasDown && s.readerOk) {
            s.reader.read(s.now);
            float me[3] = {};
            wok::mem::readBytes(s.reader.character() + 0x48, me, sizeof(me));
            const uintptr_t gw = wok::mem::moduleBase() + 0x2134110;
            struct Node {
                uintptr_t obj;
                std::string path;
            };
            std::vector<Node> frontier{{gw, "GW"}};
            std::unordered_set<uintptr_t> visited{gw};
            std::vector<std::pair<size_t, std::string>> best;
            for (int depth = 0; depth < 3 && !frontier.empty(); ++depth) {
                std::vector<Node> next;
                for (const Node& n : frontier) {
                    std::set<uintptr_t> nearby;
                    for (uintptr_t off = 0; off < 0x800; off += 8) {
                        uintptr_t p = 0;
                        if (!wok::mem::readPtr(n.obj + off, p) || p < 0x10000 || p > 0x7fffffffffff) continue;
                        if (wok::mem::isA(p, ".?AVCharacterHuman@@")) {
                            float c[3];
                            if (wok::mem::readBytes(p + 0x48, c, sizeof(c)) &&
                                (c[0] - me[0]) * (c[0] - me[0]) + (c[2] - me[2]) * (c[2] - me[2]) < 1500.0f * 1500.0f)
                                nearby.insert(p);
                            continue;
                        }
                        if (visited.size() < 300000 && visited.insert(p).second) {
                            char buf[24];
                            std::snprintf(buf, sizeof(buf), "+0x%llx", (unsigned long long)off);
                            next.push_back({p, n.path + buf});
                        }
                    }
                    if (nearby.size() >= 3) best.push_back({nearby.size(), n.path});
                }
                frontier = std::move(next);
            }
            std::sort(best.begin(), best.end(), [](auto& a, auto& b) { return a.first > b.first; });
            for (size_t i = 0; i < best.size() && i < 15; ++i)
                wok::log("probe: %zu characters near the player via %s", best[i].first, best[i].second.c_str());
            wok::log("probe: near-character search done (%zu objects visited)", visited.size());
            uintptr_t holder = 0;
            wok::mem::readPtr(gw + 0x18, holder);
            for (uintptr_t off = 0x120; off <= 0x168 && holder; off += 8) {
                uint64_t v = 0;
                wok::mem::readBytes(holder + off, &v, 8);
                size_t chars = 0;
                for (uintptr_t k = 0; k < 0x2000 && v > 0x10000 && v < 0x7fffffffffff; k += 8) {
                    uintptr_t c = 0;
                    if (wok::mem::readPtr(v + k, c) && c > 0x10000 && wok::mem::isA(c, ".?AVCharacterHuman@@")) ++chars;
                }
                wok::log("probe: *(GW+0x18)+0x%llx = %016llx (%zu characters in the next 0x2000 bytes)",
                         (unsigned long long)off, (unsigned long long)v, chars);
            }
        }
        wasDown = down;
    }
    // Direct control (dev toggle Ctrl+Shift+F3 until the Options checkbox exists).
    {
        static bool wasDown = false;
        bool down = (GetAsyncKeyState(VK_F3) & 0x8000) && (GetAsyncKeyState(VK_CONTROL) & 0x8000) &&
                    (GetAsyncKeyState(VK_SHIFT) & 0x8000);
        if (down && !wasDown) s.control.setEnabled(!s.control.enabled());
        wasDown = down;
        if (s.control.enabled() && s.readerOk) {
            s.reader.read(s.now);
            s.control.update(s.now, s.reader.character(), (s.chat && s.chat->typing()) || wok::mygui::keyFocus());
        }
    }
    if (!s.chat) s.chat = std::make_unique<wok::Chat>([](const wok::ChatSend& m) { g_state->net->sendChat(m); });
    if (s.now >= s.nextOptionsPoll) {  // our Options tab; save a changed setting
        s.nextOptionsPoll = s.now + 0.5;
        static bool loaded = false;
        if (!loaded) {
            loaded = true;
            s.options.chatOpacity = s.cfg.chatOpacity;
            s.chat->setSize(s.cfg.chatWidth, s.cfg.chatHeight);
        }
        wok::ensureOptionsTab(s.options);
        float v = s.options.chatOpacity < 20.0f ? 20.0f : (s.options.chatOpacity > 100.0f ? 100.0f : s.options.chatOpacity);
        if (static_cast<int>(v + 0.5f) != static_cast<int>(s.cfg.chatOpacity + 0.5f)) {
            s.cfg.chatOpacity = v;
            saveConfig(s.cfg);
        }
    }
    s.chat->setOpacity(s.options.chatOpacity);
    if (s.chat->takeResized()) {  // the player dragged the chat frame's corner
        s.cfg.chatWidth = s.chat->width();
        s.cfg.chatHeight = s.chat->height();
        saveConfig(s.cfg);
    }
    static bool pauseMenuOpen = false;  // Kenshi's pause (Escape) menu, checked 5 times a second
    static double nextPauseCheck = 0;
    if (s.now >= nextPauseCheck) {
        nextPauseCheck = s.now + 0.2;
        pauseMenuOpen = wok::mygui::findShownBySuffix("_ResumeButton", 3) != nullptr;
    }
    s.chat->update(s.now, connected && s.avatarsOk && s.avatars.inGame() && (s.characterRestored || s.characterSent), pauseMenuOpen);
    if (s.avatarsOk) {  // other players' names above their heads
        std::vector<wok::NameTag> tags;
        if (connected && s.avatars.inGame() && !pauseMenuOpen)
            for (auto& t : s.avatars.nameTags()) tags.push_back({t.name, {t.pos[0], t.pos[1], t.pos[2]}});
        wok::updateNameTags(tags, !tags.empty());
    }
    if (s.sharedOk && connected && s.characterRestored && !s.dead)
        s.shared.update(
            s.now, s.net->playerId(), s.reader.character(),
            [](uintptr_t ch) { return g_state->avatars.isAvatar(ch); },
            [](const wok::Message& m, bool reliable) { g_state->net->sendShared(m, reliable); });
    lap(FrameProfile::Avatars);
    g_prof.worstFrameMs = std::max(g_prof.worstFrameMs, FrameProfile::nowMs() - frameStart);

    ++s.frames;
    s.fpsTimer += dt;
    if (s.fpsTimer >= 10.0) {
        const double n = s.frames > 0 ? s.frames : 1;
        auto& m = g_prof.ms;
        wok::log("perf: %.1f fps | our code per frame (ms): probe %.3f menu %.3f send %.3f polls %.3f incoming %.3f "
                 "avatars %.3f | worst %.2f",
                 s.frames / s.fpsTimer, m[0] / n, m[1] / n, m[2] / n, m[3] / n, m[4] / n, m[5] / n, g_prof.worstFrameMs);
        g_prof = {};
        s.frames = 0;
        s.fpsTimer = 0.0;
    }
}

}  // namespace

extern "C" __declspec(dllexport) void dllStartPlugin() {
    try {
        wok::logInit();
        wok::log("World of Kenshi client loaded (protocol %u)", wok::WOK_PROTOCOL_VERSION);
        g_state = new ClientState;
        g_state->cfg = loadOrCreateConfig();
        if (g_state->cfg.servers.empty()) {  // until a server browser exists, offer the local test server
            g_state->cfg.servers.push_back({"Local server (test)", "127.0.0.1:27800"});
            saveConfig(g_state->cfg);
        }
        wok::log("config: host=%s port=%u name=%s", g_state->cfg.host.c_str(), g_state->cfg.port,
                 g_state->cfg.name.c_str());
        g_state->readerOk = g_state->reader.init();
        if (!g_state->readerOk) wok::log("local player reader disabled (signature missing)");
        g_state->avatarsOk = g_state->readerOk && g_state->avatars.init(g_state->reader.gameWorld());
        g_state->sharedOk = g_state->avatarsOk && g_state->shared.init(g_state->reader.gameWorld());
        if (wok::mygui::init())
            g_state->menu = new wok::MultiplayerMenu(
                g_state->cfg, *g_state->net, [] { saveConfig(g_state->cfg); }, [] { connectToServer(); },
                [] {  // unlink: forget every server token on this PC and disconnect
                    g_state->net->stop();
                    std::remove(tokensPath().c_str());
                    wok::log("auth: tokens forgotten (unlink)");
                },
                [] { return g_state->avatarsOk && g_state->avatars.inGame(); },
                [] {  // Disconnect from the pause menu: leave the server, forget remote players
                    g_state->net->stop();
                    g_state->avatars.clear();
                });
        if (g_state->menu) g_state->menu->setDevNoTimeLock(devSwitch("dev_no_timelock"));
        wok::installFrameHook(onFrame);
        wok::log("Multiplayer button on the main menu (F9 also connects, for development)");
    } catch (...) {
        wok::log("dllStartPlugin: unexpected exception");
    }
}

extern "C" __declspec(dllexport) void dllStopPlugin() {
    try {
        wok::log("World of Kenshi client unloading");
    } catch (...) {
    }
}
