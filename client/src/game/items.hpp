#pragma once
// Creates items with Kenshi's own item factory and hands them to characters.
// Game knowledge: docs/re/inventory.md (Kenshi 1.0.68).

#include <cstdint>
#include <string>

namespace wok {

class Items {
public:
    bool init(uintptr_t gameWorld);  // checks the game functions we call; false (logged) if unusable
    // New item from its template (GameData*), optional weapon manufacturer / model (0 = game's choice).
    // Raw factory call (see docs/re/inventory.md for what each GameData is per item kind).
    uintptr_t create(uintptr_t data, uintptr_t p4 = 0, uintptr_t p5 = 0) const;
    // Gives an item to a character the way a dialog reward does (Kenshi finds it a place).
    bool give(uintptr_t character, uintptr_t item) const;
    // GameData by FCS string ID and type (GameData + 0x50), 0 if unknown.
    uintptr_t find(const std::string& stringId, int type) const;

private:
    uintptr_t gw_ = 0;
    bool ok_ = false;
};

}  // namespace wok
