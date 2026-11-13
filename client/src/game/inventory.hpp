#pragma once
// A character's inventory as text, and its restoration with Kenshi's own item factory.
// One line per item: section, then template / aux1 / aux2 as "stringID<TAB>type", then quantity.
// Layout and functions: docs/re/inventory.md (Kenshi 1.0.68).

#include "items.hpp"

#include <cstdint>
#include <string>

namespace wok {

std::string readInventory(uintptr_t character);
// Removes what the character carries, then recreates and gives every saved item. Returns the
// number of items restored.
int restoreInventory(uintptr_t character, const std::string& saved, const Items& items);

}  // namespace wok
