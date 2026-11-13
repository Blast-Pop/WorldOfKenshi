#include <doctest/doctest.h>

#include <wok/character_store.hpp>

#include <cstdio>
#include <filesystem>

using namespace wok;

namespace {
CharacterData sample() {
    CharacterData c;
    c.name = "Aeglen";
    c.race = "17-gamedata.quack";
    c.appearance = {1, 2, 3, 0, 255};
    c.x = 10.5f;
    c.y = 1500.0f;
    c.z = -20.25f;
    c.yaw = 90.0f;
    return c;
}
AuthToken tok(uint8_t seed) {
    AuthToken t{};
    for (size_t i = 0; i < t.size(); ++i) t[i] = static_cast<uint8_t>(seed + i);
    return t;
}
}  // namespace

TEST_CASE("store_unknown_player_has_nothing") {
    CharacterStore s(":memory:");
    CHECK_FALSE(s.tokenFor(42).has_value());
    CHECK_FALSE(s.load(42).has_value());
}

TEST_CASE("store_token_roundtrip") {
    CharacterStore s(":memory:");
    s.setToken(42, tok(7));
    auto t = s.tokenFor(42);
    REQUIRE(t.has_value());
    CHECK(*t == tok(7));
    CHECK_FALSE(s.tokenFor(43).has_value());
}

TEST_CASE("store_create_then_load") {
    CharacterStore s(":memory:");
    REQUIRE(s.create(42, sample()));
    auto c = s.load(42);
    REQUIRE(c.has_value());
    CHECK(*c == sample());
}

TEST_CASE("store_create_twice_refused") {
    CharacterStore s(":memory:");
    CHECK(s.create(42, sample()));
    auto other = sample();
    other.name = "Impostor";
    CHECK_FALSE(s.create(42, other));
    CHECK(s.load(42)->name == "Aeglen");
}

TEST_CASE("store_save_position_updates_only_position") {
    CharacterStore s(":memory:");
    s.create(42, sample());
    s.savePosition(42, 1.0f, 2.0f, 3.0f, 180.0f);
    auto c = s.load(42);
    REQUIRE(c.has_value());
    CHECK(c->x == 1.0f);
    CHECK(c->y == 2.0f);
    CHECK(c->z == 3.0f);
    CHECK(c->yaw == 180.0f);
    CHECK(c->name == "Aeglen");
    CHECK(c->appearance == sample().appearance);
}

TEST_CASE("store_token_and_character_are_independent") {
    CharacterStore s(":memory:");
    s.setToken(42, tok(1));
    CHECK_FALSE(s.load(42).has_value());  // token alone = authenticated but no character yet
    s.create(42, sample());
    CHECK(*s.tokenFor(42) == tok(1));
}

TEST_CASE("store_persists_across_reopen") {
    auto path = (std::filesystem::temp_directory_path() / "wok_store_test.sqlite").string();
    std::remove(path.c_str());
    {
        CharacterStore s(path);
        s.setToken(42, tok(3));
        s.create(42, sample());
        s.savePosition(42, 7.0f, 8.0f, 9.0f, 10.0f);
    }
    {
        CharacterStore s(path);
        CHECK(*s.tokenFor(42) == tok(3));
        auto c = s.load(42);
        REQUIRE(c.has_value());
        CHECK(c->x == 7.0f);
        CHECK(c->race == "17-gamedata.quack");
    }
    std::remove(path.c_str());
}
