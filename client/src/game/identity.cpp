#include "identity.hpp"

#include "game_memory.hpp"

#include <core/appearance.hpp>

namespace wok {

namespace {

constexpr uintptr_t kCharName = 0x18;       // std::string
constexpr uintptr_t kCharGender = 0x610;    // std::string ("male" / "female")
constexpr uintptr_t kCharRace = 0xb70;      // GameData*
constexpr uintptr_t kCharHair = 0xb80;      // GameData*
constexpr uintptr_t kCharBeard = 0xbd0;     // GameData*
constexpr uintptr_t kGameDataStringId = 0x58;

// FCS string ID of the GameData pointed to by `field`, empty if none.
std::string stringIdAt(uintptr_t field) {
    uintptr_t gd = 0;
    if (!mem::readPtr(field, gd) || !gd || !mem::isA(gd, ".?AVGameData@@")) return {};
    return mem::readString(gd + kGameDataStringId, kMaxStringIdBytes);
}

}  // namespace

std::optional<CreateCharacter> readIdentity(uintptr_t character) {
    if (!character || !mem::isA(character, ".?AVCharacterHuman@@")) return std::nullopt;
    CreateCharacter c;
    c.name = mem::readString(character + kCharName, kMaxNameBytes);
    c.race = stringIdAt(character + kCharRace);
    if (c.name.empty() || c.race.empty()) return std::nullopt;

    Appearance look;
    if (auto gender = mem::readString(character + kCharGender, 16); !gender.empty()) look["gender"] = gender;
    if (auto hair = stringIdAt(character + kCharHair); !hair.empty()) look["hair"] = hair;
    if (auto beard = stringIdAt(character + kCharBeard); !beard.empty()) look["beard"] = beard;
    c.appearance = encodeAppearance(look);
    return c;
}

}  // namespace wok
