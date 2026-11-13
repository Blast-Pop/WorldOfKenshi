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
