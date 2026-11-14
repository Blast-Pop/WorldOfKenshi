#pragma once
// The exact look (face and body) of another player's avatar: their exported body file (.bod2, a
// GameDataContainer holding their appearance record) loaded like Kenshi's character editor imports
// one, then applied with Character::setAppearanceData. docs/re/character.md.

#include <cstdint>
#include <string>
#include <vector>

namespace wok {

// `bod2` is the raw body file. `tag` names the temporary file (one per remote player).
bool applyBodyFile(uintptr_t character, const std::vector<uint8_t>& bod2, const std::string& tag);

}  // namespace wok
