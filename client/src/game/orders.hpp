#pragma once
// Gives AI orders to characters with Kenshi's own task system (docs/re/orders.md, Kenshi 1.0.68):
// the character then walks/runs there with its normal animations and pathfinding.

#include <cstdint>

namespace wok {

// Orders `character` to move to `pos` (Vector3). False if the game functions are not as expected.
bool orderMoveTo(uintptr_t character, const float pos[3]);

}  // namespace wok
