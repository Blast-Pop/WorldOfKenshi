#include <doctest/doctest.h>

#include <core/appearance.hpp>

using namespace wok;

TEST_CASE("appearance_roundtrip_is_sorted_text") {
    Appearance a{{"hair", "59453-Ark Haircuts.mod"}, {"beard", "59445-Ark Haircuts.mod"}, {"gender", "male"}};
    auto blob = encodeAppearance(a);
    std::string text(blob.begin(), blob.end());
    CHECK(text == "beard=59445-Ark Haircuts.mod\ngender=male\nhair=59453-Ark Haircuts.mod\n");
    CHECK(decodeAppearance(blob) == a);
}

TEST_CASE("appearance_decode_skips_bad_lines") {
    std::string text = "hair=x\nbroken\n=novalue\nkey=\ngender=female\n";
    auto a = decodeAppearance(std::vector<uint8_t>(text.begin(), text.end()));
    CHECK(a.size() == 2);
    CHECK(a["gender"] == "female");
}

TEST_CASE("appearance_encode_respects_protocol_limit") {
    Appearance a;
    for (int i = 0; i < 1000; ++i) a["key" + std::to_string(i)] = std::string(20, 'v');
    CHECK(encodeAppearance(a).size() <= kMaxAppearanceBytes);
}

TEST_CASE("base64 round-trips binary data") {
    std::vector<uint8_t> data;
    for (int i = 0; i < 300; ++i) data.push_back(static_cast<uint8_t>(i * 37));
    for (size_t n : {0u, 1u, 2u, 3u, 4u, 299u, 300u}) {
        std::vector<uint8_t> part(data.begin(), data.begin() + n);
        CHECK(wok::base64Decode(wok::base64Encode(part)) == part);
    }
    CHECK(wok::base64Encode({'M', 'a', 'n'}) == "TWFu");
    CHECK(wok::base64Encode({'M'}) == "TQ==");
}

TEST_CASE("base64 decode rejects garbage") {
    CHECK(wok::base64Decode("not base64!").empty());
    CHECK(wok::base64Decode("TQ=").empty());
}

TEST_CASE("an exported body file fits in the appearance") {
    wok::Appearance look{{"gender", "female"}, {"bod2", wok::base64Encode(std::vector<uint8_t>(3200, 0xab))}};
    CHECK(wok::decodeAppearance(wok::encodeAppearance(look)) == look);
}
