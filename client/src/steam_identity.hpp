#pragma once
#include <cstdint>
#include <vector>

namespace wok {

// The player's SteamID64, read through the steam_api64.dll Kenshi already initialised
// (SteamClient017 / SteamUser019, see docs/m2-auth.md). 0 if Steam is unavailable.
uint64_t localSteamId();

// A Steam session ticket proving this player to a game server (ISteamUser::GetAuthSessionTicket);
// the server checks it with Steam (BeginAuthSession). Empty if Steam is unavailable.
std::vector<uint8_t> steamAuthTicket();

}  // namespace wok
