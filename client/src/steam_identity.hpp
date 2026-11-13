#pragma once
#include <cstdint>

namespace wok {

// The player's SteamID64, read through the steam_api64.dll Kenshi already initialised
// (SteamClient017 / SteamUser019, see docs/m2-auth.md). 0 if Steam is unavailable.
uint64_t localSteamId();

}  // namespace wok
