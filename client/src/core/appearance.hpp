#pragma once
// A character's look as sent to the server: "key=value" lines (sorted), values being FCS string
// IDs or plain words (e.g. hair=59453-Ark Haircuts.mod, gender=male). Opaque to the server.

#include <wok/protocol.hpp>

#include <map>
#include <string>
#include <vector>

namespace wok {

using Appearance = std::map<std::string, std::string>;

std::vector<uint8_t> encodeAppearance(const Appearance& a);  // never exceeds kMaxAppearanceBytes
Appearance decodeAppearance(const std::vector<uint8_t>& blob);

}  // namespace wok
