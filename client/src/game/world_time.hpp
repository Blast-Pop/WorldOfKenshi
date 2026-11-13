#pragma once
// Aligns Kenshi's world clock (day and hour) to the server's. Layout: docs/re/game-world.md.

#include <wok/protocol.hpp>

#include <cstdint>

namespace wok {

// Returns true when the game clock had to be moved (it differed by more than a few game minutes).
bool applyWorldTime(uintptr_t gameWorld, const WorldTime& t);

}  // namespace wok
