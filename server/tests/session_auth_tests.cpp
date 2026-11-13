#include <doctest/doctest.h>

#include <wok/character_store.hpp>
#include <wok/session.hpp>

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

TEST_CASE("wrong_token_rejected_badauth") {
    Fixture f;
    AuthToken good{}, bad{};
    good.fill(9);
    bad.fill(8);
    f.store.setToken(42, good);
    auto acks = all<HelloAck>(f.hello(1, 42, bad));
    REQUIRE(acks.size() == 1);
    CHECK(acks[0].reason == RejectReason::BadAuth);
    CHECK(f.sm.playerCount() == 0);
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
    f.sm.onPacket(1, encode(s), 1.0);
    f.sm.tick(5.0);
    CHECK(f.store.load(42)->x == sampleChar().x);  // not yet
    f.sm.onPacket(1, encode(s), 9.0);
    f.sm.tick(kPositionSaveIntervalSec + 1.0);
    CHECK(f.store.load(42)->x == 11.0f);
}
