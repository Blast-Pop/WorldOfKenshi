#pragma once
// Developer probe used to locate game structures at runtime (Task 7). Press F8 in game:
// 1st press: lists Character/Platoon objects reachable from PlayerInterface and snapshots the
//            first found character's memory; following presses: logs which float fields changed.
// Addresses are for Kenshi 1.0.68 (see docs/re/game-world.md).

namespace wok {

void probeTick();

}  // namespace wok
