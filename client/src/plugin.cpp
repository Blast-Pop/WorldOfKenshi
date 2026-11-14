// Entry points called by OGRE: Kenshi loads every "Plugin=" listed in Plugins_x64.cfg and calls
// dllStartPlugin() during Ogre::Root initialisation, dllStopPlugin() at shutdown.
// Nothing here may throw into the game or touch OGRE objects beyond registering hooks.

#include "frame_hook.hpp"
#include "game/avatars.hpp"
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
#include <map>
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

void onFrame(float dt) {
    auto& s = *g_state;
    s.now += dt;
    wok::probeTick();
    if (s.menu) s.menu->update(dt);

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
            s.net->createCharacter(*c);
            s.characterSent = true;
            s.statsSent.clear();  // the first stats poll sends every skill of the new character
            wok::log("character: sent '%s' (%s, look %zu bytes) to the server", c->name.c_str(), c->race.c_str(), c->appearance.size());
        }
    }
    s.wasConnected = connected;
    for (auto& m : s.net->drainIncoming()) {
        if (std::holds_alternative<wok::CharacterDied>(m)) {  // retired: make a new one, still connected
            s.dead = false;
            s.characterSent = false;
            s.characterRestored = false;
            s.statsSent.clear();
            if (s.menu) s.menu->restartForNewCharacter();
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
        if (auto* wt = std::get_if<wok::WorldTime>(&m); wt && s.characterRestored)
            wok::applyWorldTime(s.reader.gameWorld(), *wt);
        if (s.avatarsOk) s.avatars.onMessage(m, s.now);
    }
    if (s.avatarsOk) s.avatars.update(s.now);

    ++s.frames;
    s.fpsTimer += dt;
    if (s.fpsTimer >= 60.0) {
        wok::log("frame hook alive: %.1f fps", s.frames / s.fpsTimer);
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
