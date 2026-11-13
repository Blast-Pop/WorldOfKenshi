// wok_server — World of Kenshi dedicated server.
// Usage: wok_server [--port 27800] [--max-players 250] [--ban-file bans.txt]
//                   [--db world.sqlite] [--http-port 27801] [--public-url http://HOST:27801]
//                   [--spawn X,Y,Z]

#include <wok/auth_http.hpp>
#include <wok/character_store.hpp>
#include <wok/session.hpp>
#include <wok/transport.hpp>

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
                         "                  [--http-port N] [--public-url http://HOST:PORT] [--spawn X,Y,Z]\n");
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
            std::printf("auth page %s (TCP %u) - database %s\n", args.publicUrl.c_str(), args.httpPort,
                        args.db.c_str());
            std::fflush(stdout);
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
                flush(session.tick(now()));
                for (ConnId c : session.connectionsToClose()) server.close(c);
                std::fflush(stdout);
                std::this_thread::sleep_for(std::chrono::milliseconds(16));  // ~60 Hz
            }
            std::printf("shutting down\n");
        }
    }
    netShutdown();
    return rc;
}
