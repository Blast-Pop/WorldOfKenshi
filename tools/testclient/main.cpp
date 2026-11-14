// wok_testclient — fake player that walks in a circle. Used to test the server and the game
// client without a second copy of Kenshi.
// Usage: wok_testclient [--host 127.0.0.1] [--port 27800] [--name Bot1] [--radius 10]
//                       [--x 0 --y 0 --z 0] [--steamid N] [--seconds N]
//                       [--follow 1 [--offset 40] [--delay 0.5]]
// --follow: instead of circling, copy the first other player's moves `delay` seconds late,
// shifted by `offset` game units on X - a companion to test the game client alone.

#include <wok/protocol.hpp>
#include <wok/transport.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <functional>
#include <map>
#include <string>
#include <thread>

using namespace wok;

namespace {

std::atomic<bool> g_running{true};
void onSignal(int) { g_running = false; }

struct Args {
    std::string host = "127.0.0.1";
    uint16_t port = WOK_DEFAULT_PORT;
    std::string name = "Bot1";
    float radius = 10.0f;
    float cx = 0, cy = 0, cz = 0;
    uint64_t steamId = 0;
    double seconds = 0;  // 0 = run until Ctrl+C
    bool follow = false;
    float offset = 40.0f;
    double delay = 0.5;
    std::string ticketHex;  // Steam session ticket to present (security tests)
};

bool parseArgs(int argc, char** argv, Args& a) {
    for (int i = 1; i < argc; ++i) {
        std::string k = argv[i];
        if (i + 1 >= argc) return false;
        const char* v = argv[++i];
        if (k == "--host") a.host = v;
        else if (k == "--port") a.port = static_cast<uint16_t>(std::atoi(v));
        else if (k == "--name") a.name = v;
        else if (k == "--radius") a.radius = static_cast<float>(std::atof(v));
        else if (k == "--x") a.cx = static_cast<float>(std::atof(v));
        else if (k == "--y") a.cy = static_cast<float>(std::atof(v));
        else if (k == "--z") a.cz = static_cast<float>(std::atof(v));
        else if (k == "--steamid") a.steamId = std::strtoull(v, nullptr, 10);
        else if (k == "--ticket-hex") a.ticketHex = v;
        else if (k == "--seconds") a.seconds = std::atof(v);
        else if (k == "--follow") a.follow = std::atoi(v) != 0;
        else if (k == "--offset") a.offset = static_cast<float>(std::atof(v));
        else if (k == "--delay") a.delay = std::atof(v);
        else return false;
    }
    // Fake id in the server's test-bot range (needs wok_server --test-bots).
    if (a.steamId == 0) a.steamId = std::hash<std::string>{}(a.name) % 999999 + 1;
    return true;
}

const char* reasonText(RejectReason r) {
    switch (r) {
        case RejectReason::ServerFull: return "server full";
        case RejectReason::VersionMismatch: return "version mismatch";
        case RejectReason::Banned: return "banned";
        case RejectReason::BadAuth: return "bad auth";
        default: return "none";
    }
}

}  // namespace

int main(int argc, char** argv) {
    Args a;
    if (!parseArgs(argc, argv, a)) {
        std::fprintf(stderr, "usage: wok_testclient [--host H] [--port N] [--name S] [--radius R] [--x X --y Y --z Z] "
                             "[--steamid N] [--seconds N]\n");
        return 2;
    }
    std::signal(SIGINT, onSignal);
    if (!netInit()) return 1;
    int rc = 0;
    {
        NetClient net;
        if (!net.connect(a.host.c_str(), a.port)) {
            std::fprintf(stderr, "error: cannot resolve %s\n", a.host.c_str());
            rc = 1;
        }
        PlayerId me = 0;
        uint32_t tick = 0;
        std::map<PlayerId, double> lastPrint;
        const auto start = std::chrono::steady_clock::now();
        auto now = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
        double nextSend = 0;
        double nextPing = 0;
        PlayerId target = 0;
        std::deque<std::pair<double, PlayerState>> history;  // target's states, by arrival time

        while (rc == 0 && g_running && (a.seconds <= 0 || now() < a.seconds)) {
            for (auto& ev : net.poll()) {
                if (ev.kind == NetEvent::Connected) {
                    Hello h{};
                    h.steamId = a.steamId;
                    for (size_t i = 0; i + 1 < a.ticketHex.size(); i += 2)
                        h.ticket += static_cast<char>(std::strtol(a.ticketHex.substr(i, 2).c_str(), nullptr, 16));
                    std::strncpy(h.name, a.name.c_str(), sizeof(h.name) - 1);
                    auto b = encode(h);
                    net.send(b, true);
                    std::printf("connected, sending Hello as %s\n", a.name.c_str());
                } else if (ev.kind == NetEvent::Disconnected) {
                    std::printf("disconnected\n");
                    g_running = false;
                } else if (auto m = decode(ev.data)) {
                    if (auto* ack = std::get_if<HelloAck>(&*m)) {
                        if (ack->reason == RejectReason::None) {
                            me = ack->playerId;
                            std::printf("joined as player %u\n", me);
                        } else {
                            std::printf("rejected: %s\n", reasonText(ack->reason));
                            rc = 3;
                        }
                    } else if (std::holds_alternative<AuthChallenge>(*m)) {
                        std::printf("not let in: the server asks for a Steam login in the browser\n");
                        rc = 4;
                    } else if (auto* s = std::get_if<PlayerState>(&*m)) {
                        if (a.follow && target == 0) {
                            target = s->playerId;
                            std::printf("following player %u\n", target);
                        }
                        if (s->playerId == target) {
                            history.emplace_back(now(), *s);
                            while (history.size() > 2 && now() - history.front().first > a.delay + 2.0)
                                history.pop_front();
                        }
                        if (now() - lastPrint[s->playerId] >= 1.0) {
                            lastPrint[s->playerId] = now();
                            std::printf("player %u at (%.1f, %.1f, %.1f) yaw %.0f anim %u\n", s->playerId, s->x, s->y,
                                        s->z, s->yaw, static_cast<unsigned>(s->anim));
                        }
                    } else if (auto* l = std::get_if<PlayerLeft>(&*m)) {
                        std::printf("player %u left\n", l->playerId);
                        lastPrint.erase(l->playerId);
                        if (l->playerId == target) {
                            target = 0;
                            history.clear();
                        }
                    }
                }
            }
            if (me != 0 && a.follow && now() >= nextSend) {
                nextSend = now() + 0.1;  // 10 Hz
                const PlayerState* src = nullptr;
                for (auto& [t, st] : history)
                    if (t <= now() - a.delay) src = &st;
                if (src) {
                    PlayerState s = *src;
                    s.tick = ++tick;
                    s.x += a.offset;
                    auto b = encode(s);
                    net.send(b, false);
                }
            } else if (me != 0 && now() >= nextSend) {
                nextSend = now() + 0.1;    // 10 Hz
                const float speed = 3.0f;  // m/s along the circle
                float angle = a.radius > 0 ? static_cast<float>(now()) * speed / a.radius : 0.0f;
                PlayerState s{};
                s.tick = ++tick;
                s.x = a.cx + a.radius * std::cos(angle);
                s.y = a.cy;
                s.z = a.cz + a.radius * std::sin(angle);
                s.yaw = std::fmod(angle * 57.29578f + 90.0f, 360.0f);
                s.anim = a.radius > 0 ? AnimState::Walk : AnimState::Idle;
                auto b = encode(s);
                net.send(b, false);
            }
            if (me != 0 && now() >= nextPing) {  // keepalive: a follower with no target sends nothing else
                nextPing = now() + 1.0;
                auto b = encode(Ping{});
                net.send(b, false);
            }
            std::fflush(stdout);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        net.disconnect();
    }
    netShutdown();
    return rc;
}
