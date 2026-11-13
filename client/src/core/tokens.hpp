#pragma once
// Per-server auth tokens kept by the client in <Kenshi>\WorldOfKenshi\tokens.ini
// ("host:port=<64 hex chars>" per line). See docs/m2-auth.md.

#include <wok/protocol.hpp>

#include <map>
#include <string>
#include <string_view>

namespace wok {

using TokenFile = std::map<std::string, AuthToken>;

std::string serverKey(std::string_view host, uint16_t port);  // lower-cased "host:port"
TokenFile parseTokens(std::string_view text);                  // bad lines are skipped
std::string serializeTokens(const TokenFile& f);

}  // namespace wok
