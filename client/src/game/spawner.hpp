#pragma once
// Creates and drives real Kenshi characters used as avatars of remote players.
// Game knowledge: docs/re/spawning.md (Kenshi 1.0.68).

#include <cstdint>
#include <wok/protocol.hpp>

#include <string_view>

namespace wok {

class Spawner {
public:
    bool init(uintptr_t gameWorld);  // checks the game functions we call; false (logged) if unusable
    bool inGame() const;             // a world with a PlayerInterface is loaded

    uintptr_t findFaction(std::string_view name) const;  // by display name, 0 if absent
    // New squad of `faction` at `pos` holding one default character. Returns the Character*, 0 on failure.
    uintptr_t spawnCharacter(uintptr_t faction, const float pos[3]) const;
    bool isAlive(uintptr_t character) const;  // still a valid CharacterHuman
    // Removes a character from the world (GameWorld::destroy), e.g. a player who left.
    bool destroy(uintptr_t character) const;
    void setPosition(uintptr_t character, const float pos[3]) const;  // teleport (physics included)

    // By FCS string ID and GameData type (the game indexes its data by type; GameData + 0x50 holds
    // it, e.g. 1 for races, 0x33 for weapons). 0 if unknown.
    uintptr_t findGameData(std::string_view stringId, int type = 1) const;
    // Returning player: turn the character Kenshi's New Game produced into the server character
    // (name, then teleport to the saved position) - no second character is created.
    bool restorePlayerCharacter(uintptr_t playerCharacter, const CharacterData& data) const;
    static void setName(uintptr_t character, std::string_view name);

private:
    uintptr_t gw_ = 0;
    bool ok_ = false;
};

}  // namespace wok
