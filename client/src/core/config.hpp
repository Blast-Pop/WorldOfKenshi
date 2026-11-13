#pragma once
// Client configuration, read from <Kenshi>\WorldOfKenshi\wok_client.ini (simple key=value lines).

#include <wok/protocol.hpp>

#include <cstdint>
#include <string>
#include <string_view>

namespace wok {

struct ClientConfig {
    std::string host = "127.0.0.1";
    uint16_t port = WOK_DEFAULT_PORT;
    std::string name = "Player";
};

// Unknown keys, comments (# or ;) and malformed lines are ignored; invalid values keep defaults.
ClientConfig parseConfig(std::string_view text);
std::string serializeConfig(const ClientConfig& cfg);

}  // namespace wok
