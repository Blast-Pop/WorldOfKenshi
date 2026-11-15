// wok_server — World of Kenshi dedicated server.
// Usage: wok_server [--port 27800] [--max-players 250] [--ban-file bans.txt]
//                   [--db world.sqlite] [--http-port 27801] [--public-url http://HOST:27801]
//                   [--spawn X,Y,Z]

#include <wok/auth_http.hpp>
#include <wok/character_store.hpp>
#include <wok/session.hpp>
#include <wok/spawn_file.hpp>
#include <wok/steam_auth.hpp>
#include <wok/transport.hpp>
#include <wok/world_clock.hpp>

#include <httplib.h>

#include <memory>
#include <random>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <thread>

using namespace wok;

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

struct Args {
    uint16_t port = WOK_DEFAULT_PORT;
    uint32_t maxPlayers = 250;
    std::string banFile;
    std::string db = "world.sqlite";
    uint16_t httpPort = 27801;
    std::string publicUrl;  // default: http://127.0.0.1:<httpPort>
    float spawn[3] = {-50979.0f, 1533.0f, 2932.0f};  // first place a character was seen in testing
    bool testBots = false;
    std::vector<SpawnPoint> spawnPoints;
    std::string spawnFile;
    std::string defaultSpawn;
    bool forceSpawn = false;  // new characters always start at the spawn rule, not Kenshi's start
    std::string modsFile;     // the server's mod list, one name per line (as in Kenshi's data/__mods.list)
    std::string name = "World of Kenshi server";
    std::string description;
    bool master = false;         // also serve the master server list
    bool steam = true;           // check players' Steam session tickets (no browser login)
#ifdef _WIN32
    std::string steamApi = "steam_api64.dll";
#else
    std::string steamApi = "./libsteam_api.so";
#endif
    std::string announceUrl;     // master to announce this server to, e.g. http://1.2.3.4:27801
    WorldRules rules;            // game rates (Kenshi's advanced options), 1.25 by default
};

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        bool hasValue = i + 1 < argc;
        if (k == "--port" && hasValue)
            a.port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (k == "--max-players" && hasValue)
            a.maxPlayers = static_cast<uint32_t>(std::atoi(argv[++i]));
        else if (k == "--ban-file" && hasValue)
            a.banFile = argv[++i];
        else if (k == "--spawn-point" && hasValue) {  // "Town name:X,Y,Z", repeatable
            std::string v = argv[++i];
            size_t colon = v.rfind(':');
            SpawnPoint sp;
            if (colon == std::string::npos ||
                std::sscanf(v.c_str() + colon + 1, "%f,%f,%f", &sp.x, &sp.y, &sp.z) != 3) {
                std::fprintf(stderr, "bad --spawn-point %s (expected Name:X,Y,Z)\n", v.c_str());
                return false;
            }
            sp.name = v.substr(0, colon);
            a.spawnPoints.push_back(sp);
        } else if (k == "--spawn-file" && hasValue) {
            a.spawnFile = argv[++i];
        } else if (k == "--mods-file" && hasValue) {
            a.modsFile = argv[++i];
        } else if ((k == "--production-speed" || k == "--building-speed" || k == "--research-speed") && hasValue) {
            float v = 0;
            if (std::sscanf(argv[++i], "%f", &v) != 1 || !(v >= kMinRuleRate && v <= kMaxRuleRate)) {
                std::fprintf(stderr, "error: %s must be between %.1f and %.1f\n", k.c_str(), kMinRuleRate, kMaxRuleRate);
                return false;
            }
            (k == "--production-speed" ? a.rules.productionSpeed
                                       : k == "--building-speed" ? a.rules.buildingSpeed : a.rules.researchSpeed) = v;
        } else if (k == "--force-spawn") {
            a.forceSpawn = true;
        } else if (k == "--default-spawn" && hasValue) {
            a.defaultSpawn = argv[++i];
        } else if (k == "--name" && hasValue)
            a.name = argv[++i];
        else if (k == "--description" && hasValue)
            a.description = argv[++i];
        else if (k == "--no-steam")
            a.steam = false;
        else if (k == "--steam-api" && hasValue)
            a.steamApi = argv[++i];
        else if (k == "--master")
            a.master = true;
        else if (k == "--announce" && hasValue)
            a.announceUrl = argv[++i];
        else if (k == "--test-bots")
            a.testBots = true;
        else if (k == "--db" && hasValue)
            a.db = argv[++i];
        else if (k == "--http-port" && hasValue)
            a.httpPort = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (k == "--public-url" && hasValue)
            a.publicUrl = argv[++i];
        else if (k == "--spawn" && hasValue &&
                 std::sscanf(argv[++i], "%f,%f,%f", &a.spawn[0], &a.spawn[1], &a.spawn[2]) == 3)
            continue;
        else {
            std::fprintf(stderr,
                         "usage: wok_server [--port N] [--max-players N] [--ban-file path] [--db path]\n"
                         "                  [--http-port N] [--public-url http://HOST:PORT] [--spawn X,Y,Z]\n"
                         "                  [--test-bots] [--name TEXT] [--description TEXT]\n"
                         "                  [--spawn-point Name:X,Y,Z ...] [--spawn-file towns.txt]\n"
                         "                  [--default-spawn Name] [--force-spawn] [--mods-file path] [--master] [--announce http://MASTER:27801]\n"
                         "                  [--no-steam] [--steam-api path/to/steam_api64.dll|libsteam_api.so]\n"
                         "                  [--production-speed X] [--building-speed X] [--research-speed X] (default 1.25)\n");
            return false;
        }
    }
    if (a.publicUrl.empty()) a.publicUrl = "http://127.0.0.1:" + std::to_string(a.httpPort);
    while (!a.publicUrl.empty() && a.publicUrl.back() == '/') a.publicUrl.pop_back();
    return a.port != 0 && a.maxPlayers != 0 && a.httpPort != 0;
}

// One SteamID64 per line; blank lines and lines starting with '#' are ignored.
void loadBans(const std::string& path, SessionConfig& cfg) {
    if (path.empty()) return;
    std::ifstream in(path);
    if (!in) {
        std::fprintf(stderr, "warning: cannot read ban file %s\n", path.c_str());
        return;
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        cfg.bannedSteamIds.insert(std::strtoull(line.c_str(), nullptr, 10));
    }
}

bool isReliable(const Message& m) { return !std::holds_alternative<PlayerState>(m); }

}  // namespace

int main(int argc, char** argv) {
    Args args;
    if (!parseArgs(argc, argv, args)) return 2;

    std::unique_ptr<CharacterStore> store;
    try {
        store = std::make_unique<CharacterStore>(args.db);
    } catch (const std::exception& e) {
        std::fprintf(stderr, "error: %s\n", e.what());
        return 1;
    }
    AuthHttp http(args.httpPort, args.publicUrl);
    if (args.master) http.enableMaster();
    if (!http.start()) {
        std::fprintf(stderr, "error: cannot listen on TCP port %u (auth web page)\n", args.httpPort);
        return 1;
    }

    SessionConfig cfg;
    cfg.maxPlayers = args.maxPlayers;
    loadBans(args.banFile, cfg);
    cfg.store = store.get();
    cfg.authUrlBase = http.challengeUrlBase();
    cfg.randomToken = [] {
        static std::random_device rd;  // OS entropy (rand_s on Windows, /dev/urandom on Linux)
        AuthToken t{};
        for (auto& b : t) b = static_cast<uint8_t>(rd() & 0xff);
        return t;
    };
    cfg.spawnX = args.spawn[0];
    cfg.spawnY = args.spawn[1];
    cfg.spawnZ = args.spawn[2];
    cfg.allowTestBots = args.testBots;
    cfg.spawnPoints = args.spawnPoints;
    if (!args.spawnFile.empty()) {
        std::ifstream in(args.spawnFile, std::ios::binary);
        if (!in) {
            std::fprintf(stderr, "error: cannot read spawn file %s\n", args.spawnFile.c_str());
            return 1;
        }
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        auto pts = parseSpawnPoints(text);
        cfg.spawnPoints.insert(cfg.spawnPoints.end(), pts.begin(), pts.end());
    }
    if (!args.defaultSpawn.empty()) {
        int idx = findSpawn(cfg.spawnPoints, args.defaultSpawn);
        if (idx < 0) {
            std::fprintf(stderr, "error: default spawn '%s' is not in the spawn points\n", args.defaultSpawn.c_str());
            return 1;
        }
        // Every new character starts at the default town.
        cfg.pickSpawn = [idx](size_t) { return static_cast<size_t>(idx); };
    }
    cfg.forceSpawn = args.forceSpawn;
    cfg.rules = args.rules;
    std::printf("rules: production x%.2f, building x%.2f, research x%.2f\n", cfg.rules.productionSpeed,
                cfg.rules.buildingSpeed, cfg.rules.researchSpeed);
    if (!args.modsFile.empty()) {  // copy of a Kenshi data/__mods.list: one mod per line, in load order
        std::ifstream in(args.modsFile);
        if (!in) {
            std::fprintf(stderr, "error: cannot read the mod list %s\n", args.modsFile.c_str());
            return 1;
        }
        std::vector<std::string> mods;
        for (std::string line; std::getline(in, line);) {
            while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
            if (!line.empty() && line[0] != '#') mods.push_back(line);
        }
        std::printf("mods: %zu required (%s)\n", mods.size(), args.modsFile.c_str());
        cfg.requiredMods = std::move(mods);
    }
    static SteamAuth steamAuth;
    if (args.steam && steamAuth.start(args.steamApi, args.port)) {
        cfg.beginSteamAuth = [](uint64_t id, const std::string& t) { return steamAuth.begin(id, t); };
        cfg.steamLicense = [](uint64_t id) { return steamAuth.licence(id); };
        cfg.endSteamAuth = [](uint64_t id) { steamAuth.end(id); };
    }
    cfg.serverName = args.name;
    cfg.serverDescription = args.description;

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);

    if (!netInit()) return 1;
    int rc = 0;
    {
        NetServer server;
        if (!server.listen(args.port)) {
            std::fprintf(stderr, "error: cannot listen on UDP port %u\n", args.port);
            rc = 1;
        } else {
            std::printf("World of Kenshi server - protocol %u - UDP %u - max %u players\n", WOK_PROTOCOL_VERSION,
                        args.port, args.maxPlayers);
            std::printf("%zu spawn points\n", cfg.spawnPoints.size());
            std::printf("auth page %s (TCP %u) - database %s\n", args.publicUrl.c_str(), args.httpPort,
                        args.db.c_str());
            std::fflush(stdout);
            if (args.master) std::printf("master server list at %s/master/servers\n", args.publicUrl.c_str());
            // Announce this server to the master list every minute (players' server browsers read it).
            std::thread announcer;
            if (!args.announceUrl.empty()) {
                announcer = std::thread([&args] {
                    for (int s = 0; g_running; ++s, std::this_thread::sleep_for(std::chrono::seconds(1))) {
                        if (s % 60 != 0) continue;
                        httplib::Client master(args.announceUrl);
                        master.set_connection_timeout(5);
                        auto r = master.Post("/master/announce", "port=" + std::to_string(args.port),
                                             "application/x-www-form-urlencoded");
                        if (s == 0 || !r || r->status != 200)
                            std::printf("announce to %s: %s\n", args.announceUrl.c_str(),
                                        r && r->status == 200 ? "ok" : "FAILED");
                    }
                });
            }
            // World clock: resumes from the saved value (a new world starts on day 1 at 08:00).
            const double savedHours = store->worldHours().value_or(8.0);
            WorldClock clock(savedHours, 0.0);
            std::printf("world time: day %u, %.2f h\n", clock.message(0.0).day, clock.message(0.0).hour);
            const auto clockStart = std::chrono::steady_clock::now();
            cfg.worldTime = [&clock, clockStart](double) {
                return clock.message(std::chrono::duration<double>(std::chrono::steady_clock::now() - clockStart).count());
            };
            double nextClockSave = 60.0;
            SessionManager session(cfg);
            const auto start = std::chrono::steady_clock::now();
            auto now = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
            auto flush = [&](const std::vector<Outgoing>& out) {
                for (auto& o : out) {
                    auto bytes = encode(o.msg);
                    server.send(o.to, bytes, isReliable(o.msg));
                    if (auto* ack = std::get_if<HelloAck>(&o.msg))
                        std::printf("[%8.1f] conn %u %s (player %u, %zu online)\n", now(), o.to,
                                    ack->reason == RejectReason::None ? "joined" : "rejected", ack->playerId,
                                    session.playerCount());
                }
            };

            while (g_running) {
                for (auto& ev : server.poll()) {
                    switch (ev.kind) {
                        case NetEvent::Connected:
                            flush(session.onConnect(ev.conn, now()));
                            break;
                        case NetEvent::Packet:
                            flush(session.onPacket(ev.conn, ev.data, now()));
                            break;
                        case NetEvent::Disconnected:
                            flush(session.onDisconnect(ev.conn));
                            std::printf("[%8.1f] conn %u left (%zu online)\n", now(), ev.conn, session.playerCount());
                            break;
                    }
                }
                for (auto& [nonce, steamId] : http.poll()) flush(session.onAuthVerified(nonce, steamId, now()));
                steamAuth.runCallbacks();
                flush(session.tick(now()));
                if (now() >= nextClockSave) {  // the clock survives a server restart
                    nextClockSave = now() + 60.0;
                    store->setWorldHours(clock.gameHours(now()));
                }
                for (ConnId c : session.connectionsToClose()) server.close(c);
                std::fflush(stdout);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 Hz
            }
            std::printf("shutting down\n");
            store->setWorldHours(clock.gameHours(now()));
            if (announcer.joinable()) announcer.join();
        }
    }
    netShutdown();
    return rc;
}
