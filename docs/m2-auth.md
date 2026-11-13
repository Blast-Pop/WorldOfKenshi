# M2 — Player identity and authentication (decision, 2026-11-13)

## What Kenshi gives us (client side)

Kenshi ships an older `steam_api64.dll` and has already initialised Steam when our DLL runs. It uses
`SteamClient017`, `SteamUser019`, `SteamUtils008`; the DLL also exports the flat API
(`SteamAPI_GetHSteamUser`, `SteamAPI_GetHSteamPipe`, `SteamClient`,
`SteamAPI_ISteamClient_GetISteamUser`, `SteamAPI_ISteamUser_GetSteamID`,
`SteamAPI_ISteamUser_GetAuthSessionTicket`). So the client can read the player's **SteamID** and
produce a Steam **auth session ticket**.

## What verifying a ticket needs (server side)

A ticket is checked with `ISteamGameServer::BeginAuthSession`, which requires the dedicated server to
run the Steamworks game-server API (Steamworks SDK `steam_api64` / `steamclient`, a game-server logon
for Kenshi's app id 233860). Whether an anonymous game server for Kenshi is accepted by Steam, and the
redistribution terms of the SDK for a fan project, are not settled. The Web API alternative needs a
publisher key we do not have.

## Decision for M2

1. The client sends its **SteamID** and (when available) its **Steam ticket** in `Hello`.
2. The server identifies players by SteamID and protects each character with a **per-server secret
   token** (trust on first use): on a player's first connection the server creates the token, sends it
   once, and the client stores it in `<Kenshi>\WorldOfKenshi\tokens.ini` (one line per server). Every
   later connection must present the same token for that SteamID, or it is rejected (`BadAuth`).
3. Server-side ticket verification through the Steamworks game-server API is kept as a later,
   optional step (server config `steam_auth=on`), once tested on a real dedicated server.

## Known limit

Until Steam ticket verification is on, someone who knows another player's SteamID could register it on
a server **before** that player's first visit. Admins can delete such a record; tokens prevent any
takeover after the first visit.
