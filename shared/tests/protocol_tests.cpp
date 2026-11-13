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
