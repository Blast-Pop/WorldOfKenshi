#include <doctest/doctest.h>

#include <core/config.hpp>

using namespace wok;

TEST_CASE("config_defaults_on_empty") {
    auto c = parseConfig("");
    CHECK(c.host == "127.0.0.1");
    CHECK(c.port == 27800);
    CHECK(c.name == "Player");
}

TEST_CASE("config_reads_values_with_spaces_and_crlf") {
    auto c = parseConfig("# comment\r\nhost = 45.32.1.2\r\n; other\r\nport=27900\r\nname = Aeglen\r\n");
    CHECK(c.host == "45.32.1.2");
    CHECK(c.port == 27900);
    CHECK(c.name == "Aeglen");
}

TEST_CASE("config_ignores_invalid_port_and_unknown_keys") {
    auto c = parseConfig("port=99999\nfoo=bar\nport=abc\nnoequals\nname=\n");
    CHECK(c.port == 27800);
    CHECK(c.name == "Player");
}

TEST_CASE("config_name_truncated_to_31_chars") {
    auto c = parseConfig("name=" + std::string(40, 'x'));
    CHECK(c.name.size() == 31);
}

TEST_CASE("config_roundtrip") {
    ClientConfig c;
    c.host = "example.org";
    c.port = 1234;
    c.name = "Bob";
    auto r = parseConfig(serializeConfig(c));
    CHECK(r.host == c.host);
    CHECK(r.port == c.port);
    CHECK(r.name == c.name);
}

TEST_CASE("parse_address_host_port") {
    auto a = parseAddress("45.32.1.2:27900");
    REQUIRE(a.has_value());
    CHECK(a->host == "45.32.1.2");
    CHECK(a->port == 27900);
}

TEST_CASE("parse_address_default_port_and_trim") {
    auto a = parseAddress("  play.example.org  ");
    REQUIRE(a.has_value());
    CHECK(a->host == "play.example.org");
    CHECK(a->port == WOK_DEFAULT_PORT);
}

TEST_CASE("parse_address_rejects_garbage") {
    CHECK_FALSE(parseAddress("").has_value());
    CHECK_FALSE(parseAddress("   ").has_value());
    CHECK_FALSE(parseAddress("host:").has_value());
    CHECK_FALSE(parseAddress("host:99999").has_value());
    CHECK_FALSE(parseAddress("host:abc").has_value());
    CHECK_FALSE(parseAddress("ho st:1").has_value());
}

TEST_CASE("config_reads_server_list") {
    auto c = parseConfig("server=Local test,127.0.0.1:27800\nserver = EU #1 , 45.32.1.2:27900\nserver=broken\n");
    REQUIRE(c.servers.size() == 2);
    CHECK(c.servers[0].name == "Local test");
    CHECK(c.servers[0].address == "127.0.0.1:27800");
    CHECK(c.servers[1].name == "EU #1");
    CHECK(c.servers[1].address == "45.32.1.2:27900");
}

TEST_CASE("config_roundtrip_keeps_servers") {
    ClientConfig c;
    c.servers.push_back({"Local", "127.0.0.1:27800"});
    auto r = parseConfig(serializeConfig(c));
    REQUIRE(r.servers.size() == 1);
    CHECK(r.servers[0].name == "Local");
    CHECK(r.servers[0].address == "127.0.0.1:27800");
}
