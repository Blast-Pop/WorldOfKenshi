#pragma once
// Creates and drives real Kenshi characters used as avatars of remote players.
// Game knowledge: docs/re/spawning.md (Kenshi 1.0.68).

#include <cstdint>
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

private:
    uintptr_t gw_ = 0;
    bool ok_ = false;
};

}  // namespace wok
