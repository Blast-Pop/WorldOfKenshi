// wok_server — World of Kenshi dedicated server.
// Usage: wok_server [--port 27800] [--max-players 250] [--ban-file bans.txt]

#include <wok/session.hpp>
#include <wok/transport.hpp>

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
        else {
            std::fprintf(stderr, "usage: wok_server [--port N] [--max-players N] [--ban-file path]\n");
            return false;
        }
    }
    return a.port != 0 && a.maxPlayers != 0;
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

    SessionConfig cfg;
    cfg.maxPlayers = args.maxPlayers;
    loadBans(args.banFile, cfg);

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
