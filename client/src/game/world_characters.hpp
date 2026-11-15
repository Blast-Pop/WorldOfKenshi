#pragma once
// The characters Kenshi is currently simulating (GameWorld's character update set, a boost
// unordered_set<Character*> at GameWorld + 0x770..0x7a8). docs/re/game-world.md.

#include <cstdint>
#include <vector>

namespace wok {

// Every character in the update set (players and NPCs alike), at most `limit`.
std::vector<uintptr_t> activeCharacters(size_t limit = 2048);

}  // namespace wok
