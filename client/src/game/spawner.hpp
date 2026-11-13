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
    void setPosition(uintptr_t character, const float pos[3]) const;

    uintptr_t findGameData(std::string_view stringId) const;  // by FCS string ID, 0 if unknown
    // Returning player: recreate their server character in the squad of `playerCharacter` (the
    // character Kenshi's New Game produced), at the saved position, with its template and name.
    uintptr_t restorePlayerCharacter(uintptr_t playerCharacter, const CharacterData& data) const;
    static void setName(uintptr_t character, std::string_view name);

private:
    uintptr_t gw_ = 0;
    bool ok_ = false;
};

}  // namespace wok
