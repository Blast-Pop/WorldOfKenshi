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
    REQUIRE(!out.empty());
    CHECK(std::holds_alternative<HelloAck>(out[0].msg));
    CHECK(sentOf<PlayerInfo>(out).size() == 2);  // both players learn the other's name
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

TEST_CASE("chat_global_reaches_all_local_only_nearby") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    auto b = join(sm, 2, 12).playerId;
    auto c = join(sm, 3, 13).playerId;
    sm.onPacket(1, stateBytes(a, 0.0f), 0.1);
    sm.onPacket(2, stateBytes(b, 10.0f), 0.1);
    sm.onPacket(3, stateBytes(c, 5000.0f), 0.1);
    auto global = sentOf<ChatMessage>(sm.onPacket(1, encode(ChatSend{ChatChannel::Global, "hi"}), 1.0));
    CHECK(global.size() == 3);  // everyone, the speaker included
    auto local = sm.onPacket(1, encode(ChatSend{ChatChannel::Local, " near\x01 "}), 2.0);
    CHECK(sentOf<ChatMessage>(local).size() == 2);  // the speaker and the player 10 units away
    REQUIRE(!sentOf<ChatMessage>(local).empty());
    CHECK(sentOf<ChatMessage>(local)[0].second.text == "near");
    CHECK(sentOf<ChatMessage>(sm.onPacket(1, encode(ChatSend{ChatChannel::Global, "spam"}), 2.1)).empty());
}

TEST_CASE("cell_owner_is_the_earliest_player_and_npcs_reach_neighbours") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    auto b = join(sm, 2, 12).playerId;
    auto far = join(sm, 3, 13).playerId;
    sm.onPacket(1, stateBytes(a, 100.0f), 0.1);   // same cell as b
    sm.onPacket(2, stateBytes(b, 150.0f), 0.1);
    sm.onPacket(3, stateBytes(far, 90000.0f), 0.1);
    auto owners = sentOf<CellOwners>(sm.tick(1.0));
    REQUIRE(!owners.empty());
    const CellOwners& list = owners[0].second;
    CHECK(list.cells.size() == 18);  // two separate areas of 3 x 3 cells
    for (auto& cell : list.cells)
        if (cell.cx == cellOf(100.0f) && cell.cz == cellOf(0.0f)) CHECK(cell.owner == a);  // a joined first
    // The owner's NPCs go to b (near), not to the far player; a non-owner cannot speak for NPCs.
    NpcSnapshot snap{0, {{1, 10.f, 0.f, 0.f, 0.f, AnimState::Idle, false}}};
    auto relayed = sentOf<NpcSnapshot>(sm.onPacket(1, encode(snap), 1.1));
    REQUIRE(relayed.size() == 1);
    CHECK(relayed[0].first == 2);
    CHECK(relayed[0].second.owner == a);
    CHECK(sentOf<NpcSnapshot>(sm.onPacket(2, encode(snap), 1.2)).empty());
    CHECK(sentOf<CellOwners>(sm.tick(2.0)).empty());  // nothing changed: nothing sent
}

TEST_CASE("the_world_remembers_npcs_for_the_next_owner") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    sm.onPacket(1, stateBytes(a, 100.0f), 0.1);
    CHECK(sentOf<NpcRecords>(sm.tick(1.0)).empty());  // a new world remembers nothing
    sm.onPacket(1, encode(NpcInfo{0, 5, "Barman", "12-gamedata.base"}), 1.1);
    sm.onPacket(1, encode(NpcSnapshot{0, {{5, 300.f, 0.f, 0.f, 0.f, AnimState::Idle, false}}}), 1.2);
    sm.onDisconnect(1);
    sm.tick(2.0);
    // The next owner of the place gets the NPC where it was left, before the list that makes it owner.
    auto b = join(sm, 2, 12, WOK_PROTOCOL_VERSION, 2.5).playerId;
    sm.onPacket(2, stateBytes(b, 150.0f), 2.6);
    auto out = sm.tick(3.5);
    auto recs = sentOf<NpcRecords>(out);
    REQUIRE(recs.size() == 1);
    CHECK(recs[0].first == 2);
    REQUIRE(recs[0].second.records.size() == 1);
    const NpcRecord rec = recs[0].second.records[0];
    CHECK(rec.name == "Barman");
    CHECK(rec.x == 300.f);
    CHECK(!rec.dead);
    REQUIRE(!out.empty());
    CHECK(std::holds_alternative<NpcRecords>(out.front().msg));
    CHECK(std::holds_alternative<CellOwners>(out.back().msg));
    // b continues that record under its own key: no second record, and its changes are remembered.
    sm.onPacket(2, encode(NpcBind{{{9, rec.id}}}), 3.6);
    sm.onPacket(2, encode(NpcInfo{0, 9, "Barman", "12-gamedata.base"}), 3.7);
    sm.onPacket(2, encode(NpcSnapshot{0, {{9, 400.f, 0.f, 0.f, 0.f, AnimState::Idle, true}}}), 3.8);
    sm.onDisconnect(2);
    sm.tick(4.5);
    auto c = join(sm, 3, 13, WOK_PROTOCOL_VERSION, 5.0).playerId;
    sm.onPacket(3, stateBytes(c, 120.0f), 5.1);
    auto again = sentOf<NpcRecords>(sm.tick(6.0));
    REQUIRE(again.size() == 1);
    REQUIRE(again[0].second.records.size() == 1);
    CHECK(again[0].second.records[0].id == rec.id);
    CHECK(again[0].second.records[0].x == 400.f);
    CHECK(again[0].second.records[0].dead);
}

TEST_CASE("a_record_the_new_owner_does_not_continue_is_forgotten") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    sm.onPacket(1, stateBytes(a, 100.0f), 0.1);
    sm.tick(1.0);
    sm.onPacket(1, encode(NpcInfo{0, 5, "Barman", "12-gamedata.base"}), 1.1);
    sm.onPacket(1, encode(NpcInfo{0, 6, "Drifter", "13-gamedata.base"}), 1.1);
    sm.onPacket(1, encode(NpcSnapshot{0, {{5, 300.f, 0.f, 0.f, 0.f, AnimState::Idle, false},
                                         {6, 320.f, 0.f, 0.f, 0.f, AnimState::Idle, false}}}), 1.2);
    sm.onDisconnect(1);
    sm.tick(2.0);
    auto b = join(sm, 2, 12, WOK_PROTOCOL_VERSION, 2.5).playerId;
    sm.onPacket(2, stateBytes(b, 150.0f), 2.6);
    auto recs = sentOf<NpcRecords>(sm.tick(3.5));
    REQUIRE(recs.size() == 1);
    REQUIRE(recs[0].second.records.size() == 2);
    uint32_t barman = 0;
    for (auto& r : recs[0].second.records)
        if (r.name == "Barman") barman = r.id;
    sm.onPacket(2, encode(NpcBind{{{1, barman}}}), 3.6);  // b's game has the barman, not the drifter
    for (double t = 5.0; t < 3.5 + kNpcOfferSec + 10.0; t += 5.0) {
        sm.onPacket(2, encode(Ping{}), t);
        sm.tick(t);
    }
    sm.onDisconnect(2);
    sm.tick(200.0);
    auto c = join(sm, 3, 13, WOK_PROTOCOL_VERSION, 201.0).playerId;
    sm.onPacket(3, stateBytes(c, 120.0f), 201.1);
    auto again = sentOf<NpcRecords>(sm.tick(202.0));
    REQUIRE(again.size() == 1);
    REQUIRE(again[0].second.records.size() == 1);
    CHECK(again[0].second.records[0].name == "Barman");
}

TEST_CASE("a_newcomer_gets_the_cell_owners") {
    SessionManager sm({});
    auto a = join(sm, 1, 11).playerId;
    sm.onPacket(1, stateBytes(a, 100.0f), 0.1);
    sm.tick(1.0);  // a owns its cell
    sm.onConnect(2, 2.0);
    auto out = sm.onPacket(2, helloBytes(12), 2.0);
    REQUIRE(sentOf<CellOwners>(out).size() == 1);
    CHECK(sentOf<CellOwners>(out)[0].second.cells.size() == 9);  // a's 3 x 3 cells
}
