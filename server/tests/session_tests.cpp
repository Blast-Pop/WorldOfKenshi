#include <doctest/doctest.h>

#include <wok/session.hpp>

#include <algorithm>
#include <cstring>

using namespace wok;

namespace {

std::vector<uint8_t> helloBytes(uint64_t steamId = 1, uint16_t version = WOK_PROTOCOL_VERSION) {
    Hello h{};
    h.protocolVersion = version;
    h.steamId = steamId;
    std::strcpy(h.name, "p");
    return encode(h);
}

std::vector<uint8_t> stateBytes(PlayerId claimedId, float x) {
    PlayerState s{};
    s.playerId = claimedId;
    s.tick = 1;
    s.x = x;
    s.anim = AnimState::Walk;
    return encode(s);
}

template <class T>
std::vector<std::pair<ConnId, T>> sentOf(const std::vector<Outgoing>& out) {
    std::vector<std::pair<ConnId, T>> r;
    for (auto& o : out)
        if (auto p = std::get_if<T>(&o.msg)) r.emplace_back(o.to, *p);
    return r;
}

// Connects `conn` and sends Hello; returns the HelloAck it got.
HelloAck join(SessionManager& sm, ConnId conn, uint64_t steamId = 1, uint16_t version = WOK_PROTOCOL_VERSION,
              double now = 0.0) {
    sm.onConnect(conn, now);
    auto out = sm.onPacket(conn, helloBytes(steamId, version), now);
    auto acks = sentOf<HelloAck>(out);
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].first == conn);
    return acks[0].second;
}

bool contains(const std::vector<ConnId>& v, ConnId c) { return std::find(v.begin(), v.end(), c) != v.end(); }

}  // namespace

TEST_CASE("join_accepted_assigns_id") {
    SessionManager sm({});
    auto ack = join(sm, 100);
    CHECK(ack.reason == RejectReason::None);
    CHECK(ack.playerId == 1);
    CHECK(sm.playerCount() == 1);
}

TEST_CASE("join_rejected_when_full") {
    SessionConfig cfg;
    cfg.maxPlayers = 2;
    SessionManager sm(cfg);
    join(sm, 1, 11);
    join(sm, 2, 12);
    auto ack = join(sm, 3, 13);
    CHECK(ack.reason == RejectReason::ServerFull);
    CHECK(ack.playerId == 0);
    CHECK(sm.playerCount() == 2);
    CHECK(contains(sm.connectionsToClose(), 3));
}

TEST_CASE("join_rejected_on_version_mismatch") {
    SessionManager sm({});
    auto ack = join(sm, 1, 11, WOK_PROTOCOL_VERSION + 1);
    CHECK(ack.reason == RejectReason::VersionMismatch);
    CHECK(sm.playerCount() == 0);
    CHECK(contains(sm.connectionsToClose(), 1));
}

TEST_CASE("join_rejected_when_banned") {
    SessionConfig cfg;
    cfg.bannedSteamIds = {666};
    SessionManager sm(cfg);
    auto ack = join(sm, 1, 666);
    CHECK(ack.reason == RejectReason::Banned);
    CHECK(sm.playerCount() == 0);
    CHECK(contains(sm.connectionsToClose(), 1));
}

TEST_CASE("state_relayed_to_others_not_sender") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    join(sm, 2, 12);
    auto out = sm.onPacket(1, stateBytes(a, 5.0f), 0.1);
    auto states = sentOf<PlayerState>(out);
    REQUIRE(states.size() == 1);
    CHECK(states[0].first == 2);
    CHECK(states[0].second.playerId == a);
    CHECK(states[0].second.x == 5.0f);
}

TEST_CASE("state_playerid_is_overwritten") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    join(sm, 2, 12);
    auto states = sentOf<PlayerState>(sm.onPacket(1, stateBytes(999, 1.0f), 0.1));
    REQUIRE(states.size() == 1);
    CHECK(states[0].second.playerId == a);
}

TEST_CASE("state_before_hello_is_ignored") {
    SessionManager sm({});
    join(sm, 2, 12);
    sm.onConnect(1, 0.0);
    CHECK(sm.onPacket(1, stateBytes(1, 1.0f), 0.1).empty());
}

TEST_CASE("late_joiner_gets_existing_states") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    sm.onPacket(1, stateBytes(a, 3.0f), 0.1);
    sm.onConnect(2, 0.2);
    auto out = sm.onPacket(2, helloBytes(12), 0.2);
    REQUIRE(out.size() == 2);
    CHECK(std::holds_alternative<HelloAck>(out[0].msg));
    auto states = sentOf<PlayerState>(out);
    REQUIRE(states.size() == 1);
    CHECK(states[0].first == 2);
    CHECK(states[0].second.playerId == a);
    CHECK(states[0].second.x == 3.0f);
}

TEST_CASE("disconnect_broadcasts_player_left") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    join(sm, 2, 12);
    auto left = sentOf<PlayerLeft>(sm.onDisconnect(1));
    REQUIRE(left.size() == 1);
    CHECK(left[0].first == 2);
    CHECK(left[0].second.playerId == a);
    CHECK(sm.playerCount() == 1);
}

TEST_CASE("session_times_out_silent_player") {
    SessionManager sm({});
    auto a = join(sm, 1, 11, WOK_PROTOCOL_VERSION, 0.0).playerId;
    join(sm, 2, 12, WOK_PROTOCOL_VERSION, 0.0);
    sm.onPacket(2, stateBytes(0, 1.0f), 9.0);  // B stays alive
    CHECK(sentOf<PlayerLeft>(sm.tick(9.5)).empty());
    auto left = sentOf<PlayerLeft>(sm.tick(10.5));
    REQUIRE(left.size() == 1);
    CHECK(left[0].first == 2);
    CHECK(left[0].second.playerId == a);
    CHECK(contains(sm.connectionsToClose(), 1));
    CHECK(sm.playerCount() == 1);
}

TEST_CASE("session_kicks_after_three_bad_packets") {
    SessionManager sm({});
    join(sm, 1, 11);
    const uint8_t junk[] = {0xFF, 0x00};
    sm.onPacket(1, junk, 0.1);
    sm.onPacket(1, junk, 0.2);
    CHECK_FALSE(contains(sm.connectionsToClose(), 1));
    sm.onPacket(1, junk, 0.3);
    CHECK(contains(sm.connectionsToClose(), 1));
}

TEST_CASE("player_ids_never_reused") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    sm.onDisconnect(1);
    auto b = join(sm, 2, 12).playerId;
    CHECK(b != a);
}

TEST_CASE("ping_keeps_player_alive_and_is_not_relayed") {
    SessionManager sm({});
    join(sm, 1, 11, WOK_PROTOCOL_VERSION, 0.0);
    join(sm, 2, 12, WOK_PROTOCOL_VERSION, 0.0);
    auto ping = encode(Ping{});
    for (double t = 2.0; t <= 20.0; t += 2.0) {
        CHECK(sm.onPacket(1, ping, t).empty());
        sm.onPacket(2, ping, t);
    }
    CHECK(sentOf<PlayerLeft>(sm.tick(21.0)).empty());
    CHECK(sm.playerCount() == 2);
}

TEST_CASE("server_info_answered_before_hello") {
    SessionConfig cfg;
    cfg.maxPlayers = 40;
    cfg.serverName = "EU #1";
    cfg.serverDescription = "Vanilla rules";
    SessionManager sm(cfg);
    join(sm, 1, 11);  // one player online
    sm.onConnect(2, 0.0);
    auto out = sm.onPacket(2, encode(ServerInfoRequest{}), 0.1);
    auto infos = sentOf<ServerInfo>(out);
    REQUIRE(infos.size() == 1);
    CHECK(infos[0].first == 2);
    CHECK(infos[0].second.name == "EU #1");
    CHECK(infos[0].second.description == "Vanilla rules");
    CHECK(infos[0].second.players == 1);
    CHECK(infos[0].second.maxPlayers == 40);
    CHECK(infos[0].second.protocolVersion == WOK_PROTOCOL_VERSION);
    CHECK(sm.playerCount() == 1);  // a query is not a player
}
