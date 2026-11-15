#pragma once
// The server's game rules applied to Kenshi: the rates of its "advanced options" (production,
// building and research speed), which Kenshi keeps in globals and multiplies its work by.

#include <wok/protocol.hpp>

namespace wok {

// Writes the rates into Kenshi's globals; false if this Kenshi build is not the one they were found
// in (nothing is written then). Cheap: meant to be called again every few seconds, since starting
// or loading a game resets them.
bool applyWorldRules(const WorldRules& rules);

}  // namespace wok
