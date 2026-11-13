#include <doctest/doctest.h>

#include <core/tokens.hpp>

using namespace wok;

namespace {
AuthToken tok(uint8_t v) {
    AuthToken t{};
    t.fill(v);
    t[31] = 0x0f;
    return t;
}
}  // namespace

TEST_CASE("tokens_roundtrip") {
    TokenFile f;
    f["127.0.0.1:27800"] = tok(0xab);
    f["eu.example.org:27900"] = tok(0x01);
    auto back = parseTokens(serializeTokens(f));
    CHECK(back == f);
}

TEST_CASE("tokens_ignore_comments_bad_lines_and_bad_hex") {
    auto f = parseTokens("# comment\r\n127.0.0.1:27800=" + std::string(64, 'a') +
                         "\r\nbroken line\nhost:1=zz\nhost:2=" + std::string(63, 'a') + "\n");
    REQUIRE(f.size() == 1);
    CHECK(f["127.0.0.1:27800"][0] == 0xaa);
}

TEST_CASE("tokens_server_key") {
    CHECK(serverKey("127.0.0.1", 27800) == "127.0.0.1:27800");
    CHECK(serverKey("Play.Example.org", 27900) == "play.example.org:27900");
}
