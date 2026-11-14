#include <doctest/doctest.h>

#include <wok/protocol.hpp>

#include <cmath>
#include <cstring>
#include <limits>

using namespace wok;

namespace {

Hello makeHello() {
    Hello h{};
    h.protocolVersion = WOK_PROTOCOL_VERSION;
    h.steamId = 76561198000000001ull;
    std::strcpy(h.name, "Aeglen");
    return h;
}

PlayerState makeState() {
    PlayerState s{};
    s.playerId = 7;
    s.tick = 1234;
    s.x = 1.5f;
    s.y = -2.25f;
    s.z = 300.0f;
    s.yaw = 90.0f;
    s.anim = AnimState::Walk;
    return s;
}

std::vector<Message> allMessages() {
    return {makeHello(), HelloAck{RejectReason::None, 42}, HelloAck{RejectReason::Banned, 0}, makeState(),
            PlayerLeft{9}};
}

}  // namespace

TEST_CASE("roundtrip_each_message") {
    for (const Message& m : allMessages()) {
        auto bytes = encode(m);
        auto decoded = decode(bytes);
        REQUIRE(decoded.has_value());
        CHECK(*decoded == m);
    }
}

TEST_CASE("encode_first_byte_is_msgtype") {
    CHECK(encode(makeHello())[0] == static_cast<uint8_t>(MsgType::Hello));
    CHECK(encode(HelloAck{})[0] == static_cast<uint8_t>(MsgType::HelloAck));
    CHECK(encode(makeState())[0] == static_cast<uint8_t>(MsgType::PlayerState));
    CHECK(encode(PlayerLeft{1})[0] == static_cast<uint8_t>(MsgType::PlayerLeft));
}

TEST_CASE("decode_rejects_truncated") {
    for (const Message& m : allMessages()) {
        auto bytes = encode(m);
        for (size_t n = 0; n < bytes.size(); ++n) {
            CHECK_FALSE(decode(std::span<const uint8_t>(bytes.data(), n)).has_value());
        }
    }
}

TEST_CASE("decode_rejects_unknown_type") {
    const uint8_t b[] = {0xFF};
    CHECK_FALSE(decode(b).has_value());
}

TEST_CASE("decode_rejects_trailing_bytes") {
    auto bytes = encode(PlayerLeft{3});
    bytes.push_back(0);
    CHECK_FALSE(decode(bytes).has_value());
}

TEST_CASE("decode_rejects_bad_enum") {
    auto bytes = encode(makeState());
    bytes.back() = 7;  // anim is the last byte of a PlayerState
    CHECK_FALSE(decode(bytes).has_value());

    auto ack = encode(HelloAck{RejectReason::None, 1});
    ack[1] = 99;  // reason byte
    CHECK_FALSE(decode(ack).has_value());
}

TEST_CASE("hello_name_is_nul_terminated") {
    Hello h = makeHello();
    std::memset(h.name, 'A', sizeof(h.name));
    auto bytes = encode(h);
    CHECK_FALSE(decode(bytes).has_value());
}

TEST_CASE("decode_rejects_nonfinite_floats") {
    for (float bad : {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity()}) {
        PlayerState s = makeState();
        s.x = bad;
        CHECK_FALSE(decode(encode(s)).has_value());
        s = makeState();
        s.yaw = bad;
        CHECK_FALSE(decode(encode(s)).has_value());
    }
}

TEST_CASE("ping_roundtrip_and_type_byte") {
    auto b = encode(Ping{});
    CHECK(b.size() == 1);
    CHECK(b[0] == static_cast<uint8_t>(MsgType::Ping));
    auto m = decode(b);
    REQUIRE(m.has_value());
    CHECK(std::holds_alternative<Ping>(*m));
}

// ---------------------------------------------------------------- protocol v2 (M2)

namespace {
CharacterData makeCharacter() {
    CharacterData c;
    c.name = "Aeglen";
    c.race = "17-gamedata.quack";  // FCS stringID, never a numeric index
    c.appearance = {1, 2, 3, 250};
    c.x = -50918.8f;
    c.y = 1533.1f;
    c.z = 2932.5f;
    c.yaw = 45.0f;
    return c;
}
}  // namespace

TEST_CASE("protocol_version_is_11") { CHECK(WOK_PROTOCOL_VERSION == 11);
    HelloAck refused;
    refused.reason = RejectReason::ModMismatch;
    refused.serverMods = {"Many More Hairs", "MoodLighting"};
    CHECK(decode(encode(refused)) == std::optional<Message>(refused));
    PlayerInfo look{4, "Nine", "17-gamedata.quack", "tpl", {1, 2, 3}};
    CHECK(decode(encode(look)) == std::optional<Message>(look));
    CHECK(decode(encode(CharacterDied{})) == std::optional<Message>(CharacterDied{}));
    CreateCharacter cc;
    cc.name = "A";
    cc.race = "r";
    cc.hasStart = true;
    cc.startX = 12.5f;
    cc.startZ = -3.0f;
    CHECK(decode(encode(cc)) == std::optional<Message>(cc));
    CHECK(decode(encode(PlayerInfo{4, "Nine"})) == std::optional<Message>(PlayerInfo{4, "Nine"}));
    CHECK(decode(encode(WorldTime{12, 6.5f})) == std::optional<Message>(WorldTime{12, 6.5f}));
    CHECK_FALSE(decode(encode(WorldTime{1, 25.0f})).has_value()); }

TEST_CASE("hello_carries_token_roundtrip") {
    Hello h = makeHello();
    for (size_t i = 0; i < h.token.size(); ++i) h.token[i] = static_cast<uint8_t>(i * 7);
    auto m = decode(encode(h));
    REQUIRE(m.has_value());
    CHECK(std::get<Hello>(*m) == h);
}

TEST_CASE("helloack_v2_roundtrip") {
    HelloAck a{RejectReason::None, 3};
    a.hasCharacter = true;
    a.token[0] = 0xAB;
    a.token[31] = 0xCD;
    auto m = decode(encode(a));
    REQUIRE(m.has_value());
    CHECK(std::get<HelloAck>(*m) == a);
}

TEST_CASE("reject_reason_mod_mismatch_decodes") {
    auto m = decode(encode(HelloAck{RejectReason::ModMismatch, 0}));
    REQUIRE(m.has_value());
    CHECK(std::get<HelloAck>(*m).reason == RejectReason::ModMismatch);
}

TEST_CASE("auth_challenge_roundtrip") {
    AuthChallenge c{"http://45.32.1.2:27801/auth/start?nonce=abc123"};
    auto m = decode(encode(c));
    REQUIRE(m.has_value());
    CHECK(std::get<AuthChallenge>(*m) == c);
}

TEST_CASE("character_data_roundtrip") {
    auto c = makeCharacter();
    auto m = decode(encode(c));
    REQUIRE(m.has_value());
    CHECK(std::get<CharacterData>(*m) == c);
}

TEST_CASE("create_character_roundtrip") {
    CreateCharacter c{"Bob", "17-gamedata.quack", {9, 9}};
    auto m = decode(encode(c));
    REQUIRE(m.has_value());
    CHECK(std::get<CreateCharacter>(*m) == c);
}

TEST_CASE("v2_messages_reject_truncation") {
    for (const Message& msg : std::vector<Message>{makeCharacter(), CreateCharacter{"Bob", "r", {1}},
                                                   AuthChallenge{"http://x"}}) {
        auto bytes = encode(msg);
        for (size_t n = 0; n < bytes.size(); ++n)
            CHECK_FALSE(decode(std::span<const uint8_t>(bytes.data(), n)).has_value());
    }
}

TEST_CASE("v2_limits_enforced") {
    auto c = makeCharacter();
    c.appearance.assign(kMaxAppearanceBytes + 1, 0);
    CHECK_FALSE(decode(encode(c)).has_value());
    c = makeCharacter();
    c.race.assign(kMaxStringIdBytes + 1, 'r');
    CHECK_FALSE(decode(encode(c)).has_value());
    c = makeCharacter();
    c.name.assign(kMaxNameBytes + 1, 'n');
    CHECK_FALSE(decode(encode(c)).has_value());
    AuthChallenge a{std::string(kMaxUrlBytes + 1, 'u')};
    CHECK_FALSE(decode(encode(a)).has_value());
}

TEST_CASE("v2_rejects_nonfinite_character_position") {
    auto c = makeCharacter();
    c.z = std::numeric_limits<float>::infinity();
    CHECK_FALSE(decode(encode(c)).has_value());
}

// ---------------------------------------------------------------- server browser

TEST_CASE("server_info_request_roundtrip") {
    auto m = decode(encode(ServerInfoRequest{}));
    REQUIRE(m.has_value());
    CHECK(std::holds_alternative<ServerInfoRequest>(*m));
}

TEST_CASE("server_info_roundtrip") {
    ServerInfo info;
    info.protocolVersion = WOK_PROTOCOL_VERSION;
    info.name = "World of Kenshi - EU #1";
    info.description = "Vanilla rules, permanent world, 5 minute corpses.";
    info.players = 12;
    info.maxPlayers = 250;
    auto m = decode(encode(info));
    REQUIRE(m.has_value());
    CHECK(std::get<ServerInfo>(*m) == info);
}

TEST_CASE("server_info_limits") {
    ServerInfo info;
    info.name.assign(kMaxServerNameBytes + 1, 'n');
    CHECK_FALSE(decode(encode(info)).has_value());
    info.name = "ok";
    info.description.assign(kMaxServerDescriptionBytes + 1, 'd');
    CHECK_FALSE(decode(encode(info)).has_value());
}

TEST_CASE("character_template_id_roundtrip") {
    CharacterData c = makeCharacter();
    c.templateId = "1533662-rebirth.mod";
    auto m = decode(encode(c));
    REQUIRE(m.has_value());
    CHECK(std::get<CharacterData>(*m).templateId == "1533662-rebirth.mod");
    CreateCharacter cc{"Pao", "17-gamedata.quack", {}, "1533662-rebirth.mod"};
    auto m2 = decode(encode(cc));
    REQUIRE(m2.has_value());
    CHECK(std::get<CreateCharacter>(*m2) == cc);
}

TEST_CASE("stats_roundtrip_in_character_data_and_update") {
    CharacterData c;
    c.name = "Nine";
    c.race = "17-gamedata.quack";
    c.stats = {{1, 12.5f}, {21, 3.25f}};
    StatsUpdate u{{{18, 7.0f}, {37, 1.5f}}};
    for (const Message& m : std::vector<Message>{c, u}) {
        auto decoded = decode(encode(m));
        REQUIRE(decoded.has_value());
        CHECK(*decoded == m);
    }
}

TEST_CASE("stats_reject_bad_values") {
    StatsUpdate nan{{{1, std::numeric_limits<float>::quiet_NaN()}}};
    CHECK_FALSE(decode(encode(nan)).has_value());
    StatsUpdate badId{{{0, 1.0f}}};
    CHECK_FALSE(decode(encode(badId)).has_value());
    StatsUpdate tooMany;
    for (int i = 0; i < static_cast<int>(kMaxStats) + 1; ++i) tooMany.values.push_back({1, 1.0f});
    CHECK_FALSE(decode(encode(tooMany)).has_value());
}

TEST_CASE("inventory_roundtrip") {
    CharacterData c;
    c.name = "Nine";
    c.race = "r";
    c.inventory = {'h', 'i', 'p', '|', 'x', '\n'};
    InventoryUpdate u{{'m', 'a', 'i', 'n'}};
    for (const Message& m : std::vector<Message>{c, u}) {
        auto decoded = decode(encode(m));
        REQUIRE(decoded.has_value());
        CHECK(*decoded == m);
    }
    InventoryUpdate big{std::vector<uint8_t>(kMaxInventoryBytes + 1, 'x')};
    CHECK_FALSE(decode(encode(big)).has_value());
}
