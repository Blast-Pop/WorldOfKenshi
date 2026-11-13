#include <doctest/doctest.h>

#include <wok/session.hpp>
#include <wok/transport.hpp>

#include <chrono>
#include <cstring>
#include <map>
#include <thread>

using namespace wok;

namespace {

bool isReliable(const Message& m) { return !std::holds_alternative<PlayerState>(m); }

struct TestClient {
    NetClient net;
    uint64_t steamId;
    float x;
    PlayerId myId = 0;
    std::map<PlayerId, PlayerState> seen;

    void pump() {
        for (auto& ev : net.poll()) {
            if (ev.kind == NetEvent::Connected) {
                Hello h{};
                h.steamId = steamId;
                std::strcpy(h.name, "t");
                auto b = encode(h);
                net.send(b, true);
            } else if (ev.kind == NetEvent::Packet) {
                auto m = decode(ev.data);
                REQUIRE(m.has_value());
                if (auto* ack = std::get_if<HelloAck>(&*m)) myId = ack->playerId;
                if (auto* s = std::get_if<PlayerState>(&*m)) seen[s->playerId] = *s;
            }
        }
        if (myId != 0) {
            PlayerState s{};
            s.x = x;
            auto b = encode(s);
            net.send(b, false);
        }
    }
};

}  // namespace

TEST_CASE("loopback_two_clients_see_each_other") {
    REQUIRE(netInit());
    {
        NetServer server;
        REQUIRE(server.listen(27900));
        SessionManager session({});

        TestClient a{{}, 11, 1.0f};
        TestClient b{{}, 12, 2.0f};
        REQUIRE(a.net.connect("127.0.0.1", 27900));
        REQUIRE(b.net.connect("127.0.0.1", 27900));

        auto start = std::chrono::steady_clock::now();
        auto elapsed = [&] { return std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count(); };
        while (elapsed() < 3.0 && (a.seen.empty() || b.seen.empty())) {
            for (auto& ev : server.poll()) {
                std::vector<Outgoing> out;
                if (ev.kind == NetEvent::Connected) out = session.onConnect(ev.conn, elapsed());
                if (ev.kind == NetEvent::Packet) out = session.onPacket(ev.conn, ev.data, elapsed());
                if (ev.kind == NetEvent::Disconnected) out = session.onDisconnect(ev.conn);
                for (auto& o : out) {
                    auto bytes = encode(o.msg);
                    server.send(o.to, bytes, isReliable(o.msg));
                }
            }
            for (ConnId c : session.connectionsToClose()) server.close(c);
            a.pump();
            b.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }

        REQUIRE(a.myId != 0);
        REQUIRE(b.myId != 0);
        CHECK(a.myId != b.myId);
        REQUIRE(a.seen.count(b.myId) == 1);
        REQUIRE(b.seen.count(a.myId) == 1);
        CHECK(a.seen[b.myId].x == 2.0f);
        CHECK(b.seen[a.myId].x == 1.0f);
        CHECK(a.seen.count(a.myId) == 0);  // never receives its own state
        CHECK(a.net.connected());
        a.net.disconnect();
        b.net.disconnect();
    }
    netShutdown();
}

TEST_CASE("client_connect_rejects_bad_host") {
    REQUIRE(netInit());
    {
        NetClient c;
        CHECK_FALSE(c.connect("not an address", 27900));
        CHECK_FALSE(c.connected());
    }
    netShutdown();
}
