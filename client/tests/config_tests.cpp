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
