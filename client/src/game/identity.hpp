#pragma once
// Reads what defines a player's character (name, race, look) so it can be created on the server.
// Offsets: docs/re/character.md (Kenshi 1.0.68).

#include <wok/protocol.hpp>

#include <cstdint>
#include <optional>

namespace wok {

// nullopt if `character` is not a readable CharacterHuman with a name and a race.
std::optional<CreateCharacter> readIdentity(uintptr_t character);

}  // namespace wok
