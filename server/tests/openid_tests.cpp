#include <doctest/doctest.h>

#include <wok/openid.hpp>

using namespace wok::openid;

TEST_CASE("openid_url_encode_decode") {
    CHECK(urlEncode("a b&c=d/é") == "a%20b%26c%3Dd%2F%C3%A9");
    CHECK(urlDecode("a%20b%26c%3Dd%2F%C3%A9") == "a b&c=d/é");
    CHECK(urlDecode("a+b") == "a b");
    CHECK(urlDecode("bad%zz") == "bad%zz");
}

TEST_CASE("openid_login_url_points_to_steam") {
    auto url = steamLoginUrl("http://45.32.1.2:27801/auth/callback?nonce=ab", "http://45.32.1.2:27801/");
    CHECK(url.rfind("https://steamcommunity.com/openid/login?", 0) == 0);
    CHECK(url.find("openid.mode=checkid_setup") != std::string::npos);
    CHECK(url.find("openid.return_to=http%3A%2F%2F45.32.1.2%3A27801%2Fauth%2Fcallback%3Fnonce%3Dab") != std::string::npos);
    CHECK(url.find("openid.realm=http%3A%2F%2F45.32.1.2%3A27801%2F") != std::string::npos);
    CHECK(url.find("identifier_select") != std::string::npos);
}

TEST_CASE("openid_parse_query") {
    auto q = parseQuery("nonce=ab&openid.mode=id_res&openid.claimed_id=https%3A%2F%2Fsteamcommunity.com%2Fopenid%2Fid%2F76561198000000001");
    CHECK(q["nonce"] == "ab");
    CHECK(q["openid.mode"] == "id_res");
    CHECK(q["openid.claimed_id"] == "https://steamcommunity.com/openid/id/76561198000000001");
}

TEST_CASE("openid_steamid_from_claimed_id") {
    CHECK(steamIdFromClaimedId("https://steamcommunity.com/openid/id/76561198000000001") == 76561198000000001ull);
    CHECK_FALSE(steamIdFromClaimedId("https://evil.example/openid/id/76561198000000001").has_value());
    CHECK_FALSE(steamIdFromClaimedId("https://steamcommunity.com/openid/id/123").has_value());
    CHECK_FALSE(steamIdFromClaimedId("https://steamcommunity.com/openid/id/7656119800000000x").has_value());
    CHECK_FALSE(steamIdFromClaimedId("http://steamcommunity.com/openid/id/76561198000000001").has_value());
}

TEST_CASE("openid_callback_checks_endpoint_and_return_to") {
    Params p{{"openid.mode", "id_res"},
             {"openid.op_endpoint", "https://steamcommunity.com/openid/login"},
             {"openid.return_to", "http://srv:27801/auth/callback?nonce=ab"},
             {"openid.claimed_id", "https://steamcommunity.com/openid/id/76561198000000001"}};
    CHECK(callbackLooksValid(p, "http://srv:27801/auth/callback"));
    auto bad = p;
    bad["openid.op_endpoint"] = "https://evil.example/openid/login";
    CHECK_FALSE(callbackLooksValid(bad, "http://srv:27801/auth/callback"));
    bad = p;
    bad["openid.return_to"] = "http://evil.example/auth/callback?nonce=ab";
    CHECK_FALSE(callbackLooksValid(bad, "http://srv:27801/auth/callback"));
    bad = p;
    bad["openid.mode"] = "cancel";
    CHECK_FALSE(callbackLooksValid(bad, "http://srv:27801/auth/callback"));
}

TEST_CASE("openid_check_authentication_body") {
    Params p{{"nonce", "ab"}, {"openid.mode", "id_res"}, {"openid.sig", "x/y="}};
    auto body = checkAuthenticationBody(p);
    CHECK(body.find("openid.mode=check_authentication") != std::string::npos);
    CHECK(body.find("openid.sig=x%2Fy%3D") != std::string::npos);
    CHECK(body.find("nonce=") == std::string::npos);  // only openid.* fields are sent back
}

TEST_CASE("openid_is_valid_response") {
    CHECK(isValidResponse("ns:http://specs.openid.net/auth/2.0\nis_valid:true\n"));
    CHECK_FALSE(isValidResponse("ns:http://specs.openid.net/auth/2.0\nis_valid:false\n"));
    CHECK_FALSE(isValidResponse("garbage is_valid:true-ish"));
}
