#pragma once
// Client configuration, read from <Kenshi>\WorldOfKenshi\wok_client.ini (simple key=value lines).

#include <wok/protocol.hpp>

#include <cstdint>
#include <string>
#include <optional>
#include <string_view>
#include <vector>

namespace wok {

struct ServerEntry {
    std::string name;
    std::string address;  // "host:port" or "host"
};

// Master server list read by the server browser (any World of Kenshi server started with --master).
// Test phase: the EU test server; to move to a permanent domain.
inline constexpr const char* kDefaultMasterUrl = "http://95.179.253.252:27801";

struct ClientConfig {
    std::string host = "127.0.0.1";
    uint16_t port = WOK_DEFAULT_PORT;
    std::string name = "Player";
    std::vector<ServerEntry> servers;  // "server=Name,host:port" lines, shown in addition to the master list
    std::string masterUrl = kDefaultMasterUrl;  // "master=" (empty value keeps the default)
    float chatOpacity = 85.0f;                  // "chat_opacity=", percent (20..100)
    int chatWidth = 450, chatHeight = 200;      // "chat_size=W,H", pixels
    float chatFadeSec = 20.0f;                  // "chat_fade=" seconds (5..120)
    bool showNames = true;                      // "show_names=0/1"
    float nameRange = 500.0f;                   // "name_range=" game units (50..2000)
};

struct Address {
    std::string host;
    uint16_t port;
};

// "host[:port]" with surrounding spaces allowed; nullopt if empty, port invalid or host has spaces.
std::optional<Address> parseAddress(std::string_view text);

// Unknown keys, comments (# or ;) and malformed lines are ignored; invalid values keep defaults.
ClientConfig parseConfig(std::string_view text);
std::string serializeConfig(const ClientConfig& cfg);

// The master's "host:port" lines; invalid lines and duplicates are dropped (names stay empty).
std::vector<ServerEntry> parseServerList(std::string_view text);

}  // namespace wok
