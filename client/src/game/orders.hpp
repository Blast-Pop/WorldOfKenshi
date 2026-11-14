#pragma once
// Gives AI orders to characters with Kenshi's own task system (docs/re/orders.md, Kenshi 1.0.68):
// the character then walks/runs there with its normal animations and pathfinding.

#include <cstdint>

namespace wok {

// Orders `character` to move to `pos` (Vector3). False if the game functions are not as expected.
bool orderMoveTo(uintptr_t character, const float pos[3]);

// The character's movement component (CharMovement), found by its vtable; 0 if none.
uintptr_t charMovement(uintptr_t character);

// CharMovement::setDestination (vtable + 0x90): walks the character to `pos` with Kenshi's own
// pathfinding and animations. `mode` is Kenshi's movement mode argument (under test).
bool setDestination(uintptr_t character, const float pos[3], int mode);

}  // namespace wok
