#pragma once
// Creates items with Kenshi's own item factory and hands them to characters.
// Game knowledge: docs/re/inventory.md (Kenshi 1.0.68).

#include <cstdint>

namespace wok {

class Items {
public:
    bool init(uintptr_t gameWorld);  // checks the game functions we call; false (logged) if unusable
    // New item from its template (GameData*), optional weapon manufacturer / model (0 = game's choice).
    uintptr_t create(uintptr_t itemData, uintptr_t manufacturer = 0, uintptr_t model = 0) const;
    // Gives an item to a character the way a dialog reward does (Kenshi finds it a place).
    bool give(uintptr_t character, uintptr_t item) const;

private:
    uintptr_t gw_ = 0;
    bool ok_ = false;
};

}  // namespace wok
