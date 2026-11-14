#include <doctest/doctest.h>

#include <wok/character_store.hpp>
#include <wok/session.hpp>

#include <algorithm>
#include <map>
#include <cstring>

using namespace wok;

namespace {

struct Fixture {
    CharacterStore store{":memory:"};
    uint8_t counter = 0;
    SessionManager sm{config()};

    SessionConfig config() {
        SessionConfig c;
        c.store = &store;
        c.authUrlBase = "http://srv:27801/auth/start?nonce=";
        c.randomToken = [this] {
            AuthToken t{};
            t.fill(++counter);
            return t;
        };
        c.spawnX = 100.0f;
        c.spawnY = 1500.0f;
        c.spawnZ = -200.0f;
        return c;
    }

    std::vector<Outgoing> hello(ConnId conn, uint64_t steamId, const AuthToken& token = {}) {
        sm.onConnect(conn, 0.0);
        Hello h{};
        h.steamId = steamId;
        std::strcpy(h.name, "p");
        h.token = token;
        return sm.onPacket(conn, encode(h), 0.0);
    }
};

template <class T>
std::vector<T> all(const std::vector<Outgoing>& out) {
    std::vector<T> r;
    for (auto& o : out)
        if (auto p = std::get_if<T>(&o.msg)) r.push_back(*p);
    return r;
}

std::string nonceOf(const std::vector<Outgoing>& out) {
    auto ch = all<AuthChallenge>(out);
    REQUIRE(ch.size() == 1);
    auto pos = ch[0].url.find("nonce=");
    REQUIRE(pos != std::string::npos);
    return ch[0].url.substr(pos + 6);
}

CharacterData sampleChar() {
    CharacterData c;
    c.name = "Aeglen";
    c.race = "17-gamedata.quack";
    c.appearance = {4, 5, 6};
    c.x = 1.0f;
    c.y = 2.0f;
    c.z = 3.0f;
    c.yaw = 4.0f;
    return c;
}

}  // namespace

TEST_CASE("first_visit_gets_auth_challenge_not_accepted") {
    Fixture f;
    auto out = f.hello(1, 42);
    CHECK(all<HelloAck>(out).empty());
    auto ch = all<AuthChallenge>(out);
    REQUIRE(ch.size() == 1);
    CHECK(ch[0].url.rfind("http://srv:27801/auth/start?nonce=", 0) == 0);
    CHECK(f.sm.playerCount() == 0);
}

TEST_CASE("auth_verified_issues_token_and_accepts") {
    Fixture f;
    auto nonce = nonceOf(f.hello(1, 42));
    auto out = f.sm.onAuthVerified(nonce, 42, 1.0);
    auto acks = all<HelloAck>(out);
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].reason == RejectReason::None);
    CHECK(acks[0].playerId != 0);
    CHECK_FALSE(acks[0].hasCharacter);
    CHECK(acks[0].token != AuthToken{});
    CHECK(f.store.tokenFor(42) == acks[0].token);
    CHECK(f.sm.playerCount() == 1);
}

TEST_CASE("verified_steamid_wins_over_claimed_one") {
    Fixture f;
    auto nonce = nonceOf(f.hello(1, 42));  // claims 42
    f.sm.onAuthVerified(nonce, 777, 1.0);  // Steam says 777
    CHECK(f.store.tokenFor(777).has_value());
    CHECK_FALSE(f.store.tokenFor(42).has_value());
}

TEST_CASE("unknown_nonce_is_ignored") {
    Fixture f;
    f.hello(1, 42);
    CHECK(f.sm.onAuthVerified("nope", 42, 1.0).empty());
    CHECK(f.sm.playerCount() == 0);
}

TEST_CASE("returning_player_with_token_accepted_without_challenge") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    auto out = f.hello(1, 42, t);
    CHECK(all<AuthChallenge>(out).empty());
    auto acks = all<HelloAck>(out);
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].reason == RejectReason::None);
    CHECK(acks[0].token == AuthToken{});  // no new token issued
}

TEST_CASE("wrong_token_asks_steam_again") {
    // A stale or forged token never lets the player in: Steam must prove the account again.
    Fixture f;
    AuthToken good{}, bad{};
    good.fill(9);
    bad.fill(8);
    f.store.setToken(42, good);
    auto out = f.hello(1, 42, bad);
    CHECK(all<HelloAck>(out).empty());
    CHECK(all<AuthChallenge>(out).size() == 1);
    CHECK(f.sm.playerCount() == 0);
}

TEST_CASE("token_unknown_to_this_server_asks_steam_again") {
    // E.g. the server's database was reset: the player's saved token means nothing here.
    Fixture f;
    AuthToken t{};
    t.fill(7);
    auto out = f.hello(1, 42, t);
    CHECK(all<HelloAck>(out).empty());
    CHECK(all<AuthChallenge>(out).size() == 1);
}

TEST_CASE("returning_player_gets_character_data") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    f.store.create(42, sampleChar());
    auto out = f.hello(1, 42, t);
    auto acks = all<HelloAck>(out);
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].hasCharacter);
    auto chars = all<CharacterData>(out);
    REQUIRE(chars.size() == 1);
    CHECK(chars[0] == sampleChar());
}

TEST_CASE("create_character_stores_at_spawn_point_and_returns_it") {
    Fixture f;
    auto nonce = nonceOf(f.hello(1, 42));
    f.sm.onAuthVerified(nonce, 42, 1.0);
    auto out = f.sm.onPacket(1, encode(CreateCharacter{"Aeglen", "17-gamedata.quack", {7}}), 2.0);
    auto chars = all<CharacterData>(out);
    REQUIRE(chars.size() == 1);
    CHECK(chars[0].name == "Aeglen");
    CHECK(chars[0].x == 100.0f);
    CHECK(chars[0].z == -200.0f);
    REQUIRE(f.store.load(42).has_value());
}

TEST_CASE("create_character_twice_is_ignored") {
    Fixture f;
    auto nonce = nonceOf(f.hello(1, 42));
    f.sm.onAuthVerified(nonce, 42, 1.0);
    f.sm.onPacket(1, encode(CreateCharacter{"First", "r", {}}), 2.0);
    CHECK(f.sm.onPacket(1, encode(CreateCharacter{"Second", "r", {}}), 3.0).empty());
    CHECK(f.store.load(42)->name == "First");
}

TEST_CASE("create_character_before_auth_is_ignored") {
    Fixture f;
    f.hello(1, 42);
    CHECK(f.sm.onPacket(1, encode(CreateCharacter{"X", "r", {}}), 1.0).empty());
    CHECK_FALSE(f.store.load(42).has_value());
}

TEST_CASE("position_persisted_on_disconnect") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    f.store.create(42, sampleChar());
    f.hello(1, 42, t);
    PlayerState s{};
    s.x = 55.0f;
    s.y = 66.0f;
    s.z = 77.0f;
    s.yaw = 88.0f;
    f.sm.onPacket(1, encode(s), 1.0);
    f.sm.onDisconnect(1);
    auto c = f.store.load(42);
    REQUIRE(c.has_value());
    CHECK(c->x == 55.0f);
    CHECK(c->z == 77.0f);
    CHECK(c->yaw == 88.0f);
}

TEST_CASE("position_persisted_periodically") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    f.store.create(42, sampleChar());
    f.hello(1, 42, t);
    PlayerState s{};
    s.x = 11.0f;
    f.sm.onPacket(1, encode(s), 0.5);
    f.sm.tick(kProgressSaveIntervalSec - 0.5);
    CHECK(f.store.load(42)->x == sampleChar().x);  // not yet
    f.sm.onPacket(1, encode(s), kProgressSaveIntervalSec);
    f.sm.tick(kProgressSaveIntervalSec + 1.0);
    CHECK(f.store.load(42)->x == 11.0f);
}

TEST_CASE("test_bots_need_the_server_flag") {
    Fixture f;
    auto out = f.hello(1, 7);  // fake id in the test-bot range
    CHECK_FALSE(all<AuthChallenge>(out).empty());  // flag off: treated like anyone else
}

TEST_CASE("test_bots_accepted_without_auth_when_enabled") {
    CharacterStore store(":memory:");
    SessionConfig c;
    c.store = &store;
    c.allowTestBots = true;
    c.randomToken = [] { return AuthToken{}; };
    SessionManager sm(c);
    sm.onConnect(1, 0.0);
    Hello h{};
    h.steamId = 7;
    std::strcpy(h.name, "Bot");
    auto out = sm.onPacket(1, encode(h), 0.0);
    auto acks = all<HelloAck>(out);
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].reason == RejectReason::None);
    CHECK_FALSE(acks[0].hasCharacter);
    // A real SteamID is never treated as a bot.
    sm.onConnect(2, 0.0);
    h.steamId = 76561198000000001ull;
    CHECK_FALSE(all<AuthChallenge>(sm.onPacket(2, encode(h), 0.0)).empty());
}

TEST_CASE("reconnect_during_steam_login_reuses_the_challenge") {
    Fixture f;
    auto first = nonceOf(f.hello(1, 42));
    f.sm.onDisconnect(1);                     // the player clicked Connect again
    auto second = nonceOf(f.hello(2, 42));    // same claimed SteamID
    CHECK(second == first);                   // same Steam page, no new tab
    auto acks = all<HelloAck>(f.sm.onAuthVerified(first, 42, 5.0));
    REQUIRE(acks.size() == 1);                // login from the first tab completes the new connection
    CHECK(acks[0].reason == RejectReason::None);
    CHECK(f.store.tokenFor(42).has_value());
}

TEST_CASE("pending_challenge_expires") {
    Fixture f;
    auto first = nonceOf(f.hello(1, 42));
    f.sm.onDisconnect(1);
    f.sm.tick(kPendingAuthTtlSec + 1.0);
    auto second = nonceOf(f.hello(2, 42));
    CHECK(second != first);
}

TEST_CASE("new_login_closes_previous_connection_of_same_account") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    f.hello(1, 42, t);
    f.hello(2, 42, t);  // same account logs in again
    auto closing = f.sm.connectionsToClose();
    CHECK(std::find(closing.begin(), closing.end(), ConnId{1}) != closing.end());
    CHECK(f.sm.playerCount() == 1);
}

TEST_CASE("new_character_spawns_at_one_of_the_town_spawn_points") {
    CharacterStore store(":memory:");
    SessionConfig c;
    c.store = &store;
    c.allowTestBots = false;
    c.authUrlBase = "http://srv:27801/auth/start?nonce=";
    c.randomToken = [] {
        AuthToken t{};
        t.fill(1);
        return t;
    };
    c.spawnPoints = {{"The Hub", 10.0f, 1.0f, 20.0f}, {"Squin", 30.0f, 2.0f, 40.0f}};
    c.pickSpawn = [](size_t count) { return count - 1; };  // deterministic for the test: last one
    SessionManager sm(c);
    sm.onConnect(1, 0.0);
    Hello h{};
    h.steamId = 42;
    std::strcpy(h.name, "p");
    auto ch = all<AuthChallenge>(sm.onPacket(1, encode(h), 0.0));
    REQUIRE(ch.size() == 1);
    sm.onAuthVerified(ch[0].url.substr(ch[0].url.find("nonce=") + 6), 42, 1.0);
    auto out = sm.onPacket(1, encode(CreateCharacter{"Aeglen", "r", {}}), 2.0);
    auto chars = all<CharacterData>(out);
    REQUIRE(chars.size() == 1);
    CHECK(chars[0].x == 30.0f);
    CHECK(chars[0].z == 40.0f);
}

TEST_CASE("stats_update_saved_in_batches_and_on_disconnect") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    REQUIRE(f.store.create(42, sampleChar()));
    f.hello(1, 42, t);
    f.sm.onPacket(1, encode(StatsUpdate{{{1, 5.5f}}}), 0.5);
    CHECK(f.store.load(42)->stats.empty());  // not written at once
    f.sm.tick(kProgressSaveIntervalSec + 1.0);
    CHECK(f.store.load(42)->stats == std::vector<StatValue>{{1, 5.5f}});
    f.sm.onPacket(1, encode(StatsUpdate{{{1, 6.0f}, {21, 2.0f}}}), kProgressSaveIntervalSec + 1.5);
    f.sm.onDisconnect(1);  // written immediately when the player leaves
    CHECK(f.store.load(42)->stats == std::vector<StatValue>{{1, 6.0f}, {21, 2.0f}});
}

TEST_CASE("returning_player_gets_saved_stats") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    REQUIRE(f.store.create(42, sampleChar()));
    f.store.saveStats(42, {{18, 9.0f}});
    auto chars = all<CharacterData>(f.hello(1, 42, t));
    REQUIRE(chars.size() == 1);
    CHECK(chars[0].stats == std::vector<StatValue>{{18, 9.0f}});
}

TEST_CASE("inventory_saved_in_batches_and_returned") {
    Fixture f;
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    REQUIRE(f.store.create(42, sampleChar()));
    f.hello(1, 42, t);
    std::vector<uint8_t> inv{'h', 'i', 'p'};
    f.sm.onPacket(1, encode(InventoryUpdate{inv}), 0.5);
    CHECK(f.store.load(42)->inventory.empty());
    f.sm.tick(kProgressSaveIntervalSec + 1.0);
    CHECK(f.store.load(42)->inventory == inv);
    f.sm.onDisconnect(1);
    auto chars = all<CharacterData>(f.hello(2, 42, t));
    REQUIRE(chars.size() == 1);
    CHECK(chars[0].inventory == inv);
}

TEST_CASE("players_get_the_world_time_on_join_and_regularly") {
    Fixture f;
    // (rebuild the manager with a clock: the fixture's config has none)
    SessionConfig cfg = f.config();
    cfg.worldTime = [](double now) { return WorldTime{3, static_cast<float>(now)}; };
    SessionManager sm(cfg);
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    REQUIRE(f.store.create(42, sampleChar()));
    sm.onConnect(1, 0.0);
    Hello h{};
    h.steamId = 42;
    std::strcpy(h.name, "p");
    h.token = t;
    auto joined = all<WorldTime>(sm.onPacket(1, encode(h), 1.0));
    REQUIRE(joined.size() == 1);
    CHECK(joined[0].day == 3);
    CHECK(all<WorldTime>(sm.tick(1.5)).empty());
    sm.onPacket(1, encode(Ping{}), kWorldTimeIntervalSec + 1.0);  // keep the player alive
    CHECK(all<WorldTime>(sm.tick(kWorldTimeIntervalSec + 1.0)).size() == 1);
}

TEST_CASE("steam_ticket_logs_in_without_browser_and_refuses_non_owners") {
    Fixture f;
    SessionConfig cfg = f.config();
    std::map<uint64_t, int> licence;  // what "Steam" answers per SteamID
    cfg.beginSteamAuth = [](uint64_t, const std::string& t) { return t == "valid"; };
    cfg.steamLicense = [&licence](uint64_t id) { return licence.count(id) ? licence[id] : 2; };
    SessionManager sm(cfg);
    auto helloWith = [&](ConnId conn, uint64_t id, const std::string& ticket) {
        sm.onConnect(conn, 0.0);
        Hello h{};
        h.steamId = id;
        std::strcpy(h.name, "p");
        h.ticket = ticket;
        return sm.onPacket(conn, encode(h), 0.0);
    };
    CHECK(helloWith(1, 76561198000000001ull, "valid").empty());  // waiting for Steam
    CHECK(all<HelloAck>(sm.tick(0.5)).empty());
    licence[76561198000000001ull] = 0;  // Steam: owns Kenshi
    auto acks = all<HelloAck>(sm.tick(1.0));
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].reason == RejectReason::None);
    CHECK(all<AuthChallenge>(sm.tick(1.0)).empty());  // never a browser step

    helloWith(2, 76561198000000002ull, "valid");
    licence[76561198000000002ull] = 1;  // Steam: does not own Kenshi
    auto refused = all<HelloAck>(sm.tick(1.0));
    REQUIRE(refused.size() == 1);
    CHECK(refused[0].reason == RejectReason::BadAuth);

    // Steam unreachable or a bad ticket: the browser login of this server, as before.
    CHECK(all<AuthChallenge>(helloWith(3, 76561198000000003ull, "bad")).size() == 1);
}

TEST_CASE("players_learn_each_others_names") {
    Fixture f;
    AuthToken t1{}, t2{};
    t1.fill(9);
    t2.fill(10);
    f.store.setToken(42, t1);
    f.store.setToken(43, t2);
    CharacterData a = sampleChar(), b = sampleChar();
    a.name = "Nine";
    b.name = "Pao";
    REQUIRE(f.store.create(42, a));
    REQUIRE(f.store.create(43, b));
    f.hello(1, 42, t1);
    auto out = f.hello(2, 43, t2);
    bool newcomerLearnt = false, firstLearnt = false;
    for (auto& o : out)
        if (auto* p = std::get_if<PlayerInfo>(&o.msg)) {
            if (o.to == 2 && p->name == "Nine") newcomerLearnt = true;
            if (o.to == 1 && p->name == "Pao") firstLearnt = true;
        }
    CHECK(newcomerLearnt);
    CHECK(firstLearnt);
}

TEST_CASE("death_is_permanent_and_the_player_stays_connected") {
    Fixture f;
    SessionConfig cfg = f.config();
    SessionManager sm(cfg);
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    REQUIRE(f.store.create(42, sampleChar()));
    sm.onConnect(1, 0.0);
    Hello h{};
    h.steamId = 42;
    std::strcpy(h.name, "p");
    h.token = t;
    sm.onPacket(1, encode(h), 0.0);
    auto out = sm.onPacket(1, encode(CharacterDied{}), 1.0);
    CHECK(all<CharacterDied>(out).size() == 1);
    CHECK(!f.store.load(42));                 // gone from the server
    CreateCharacter cc;
    cc.name = "Second";
    cc.race = "r";
    CHECK(all<CharacterData>(sm.onPacket(1, encode(cc), 2.0)).size() == 1);  // a new one, same connection
}

TEST_CASE("new_character_keeps_its_kenshi_start_position") {
    Fixture f;
    SessionConfig cfg = f.config();
    cfg.spawnPoints = {{"The Hub", 1, 2, 3}};
    SessionManager sm(cfg);
    AuthToken t{};
    t.fill(9);
    f.store.setToken(42, t);
    sm.onConnect(1, 0.0);
    Hello h{};
    h.steamId = 42;
    std::strcpy(h.name, "p");
    h.token = t;
    sm.onPacket(1, encode(h), 0.0);
    CreateCharacter cc;
    cc.name = "Fall";
    cc.race = "r";
    cc.hasStart = true;
    cc.startX = 500.0f;
    cc.startY = 20.0f;
    cc.startZ = -700.0f;
    auto made = all<CharacterData>(sm.onPacket(1, encode(cc), 1.0));
    REQUIRE(made.size() == 1);
    CHECK(made[0].x == 500.0f);
    CHECK(made[0].z == -700.0f);
}
