#pragma once
// Reads what defines a player's character (name, race, look) so it can be created on the server.
// Offsets: docs/re/character.md (Kenshi 1.0.68).

#include <wok/protocol.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace wok {

// nullopt if `character` is not a readable CharacterHuman with a name and a race.
std::optional<CreateCharacter> readIdentity(uintptr_t character);

// Kenshi's character editor "Export" writes the whole look (race, sliders, hair...) to
// <Kenshi>/data/character/bodies/export/<name>.bod2; "Import" reads files from there.
std::string exportedBodyPath(std::string_view characterName);
std::vector<uint8_t> readExportedBody(std::string_view characterName);  // empty if missing

}  // namespace wok
