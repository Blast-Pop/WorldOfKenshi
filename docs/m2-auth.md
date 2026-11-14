# M2 — Player identity and authentication (decision, 2026-11-13)

## Client side: what Kenshi gives us

Kenshi ships an older `steam_api64.dll` and has already initialised Steam when our DLL runs
(`SteamClient017`, `SteamUser019`, `SteamUtils008`; flat exports such as `SteamAPI_ISteamUser_GetSteamID`).
The client can read the player's **SteamID**; it is a claim, not a proof.

## Proof of ownership: Steam OpenID (suggested by Aeglen)

Steam's OpenID 2.0 provider (`https://steamcommunity.com/openid/`) lets a website prove that a visitor
owns a Steam account, **without a publisher key** and without the Steamworks SDK:

1. First connection to a server, no token yet: the server answers `Hello` with an `AuthChallenge`
   carrying a one-time login URL (`http://<server>:<http-port>/auth/start?nonce=…`).
2. The game opens it in the player's browser. The page redirects to Steam's OpenID login
   (`openid.mode=checkid_setup`, `return_to` = our callback, `realm` = the server's address). The player
   signs in on steamcommunity.com — the server never sees a password.
3. Steam redirects back to `/auth/callback`. The server re-posts the received parameters to Steam with
   `openid.mode=check_authentication`; only an `is_valid:true` answer is accepted. The SteamID is the
   last part of `openid.claimed_id` (`https://steamcommunity.com/openid/id/<steamid64>`).
4. The server binds that SteamID to the waiting connection (via the nonce), creates the player's
   **per-server secret token**, and sends it in `HelloAck`. The client keeps it in
   `<Kenshi>\WorldOfKenshi\tokens.ini` (one line per server).
5. Later connections present SteamID + token: no browser step. A wrong token → `BadAuth`.

## Server requirements

- An HTTP endpoint (library `cpp-httplib`, HTTPS client through the OpenSSL we already build) on a port
  of its own (default **27801/TCP**), reachable by players' browsers.
- Outbound HTTPS to steamcommunity.com.
- Config: `--http-port`, `--public-url` (what goes into `realm`/`return_to`).

## Not chosen

- Steamworks game-server ticket validation (`BeginAuthSession`): needs the SDK and a game-server logon
  for Kenshi's app id; unsettled.
- Steam Web API ticket check: needs a publisher key.

## Update 2026-11-13: Steam session tickets work (one login for every server, no central server)

Aeglen rejected any central "official" login service. The `BeginAuthSession` route was therefore
prototyped (`tools/steamauth_probe`) and **works** with the `steam_api64.dll` Kenshi ships:

- Client: `ISteamUser::GetAuthSessionTicket` (SteamUser019) gives a ~234-byte ticket.
- Server: `SteamInternal_GameServer_Init` (authentication mode) with app id 233860, an **anonymous**
  game-server logon (`LogOnAnonymous`), then `BeginAuthSession(ticket, steamId)` → OK, and
  `UserHasLicenseForApp(steamId, 233860)` → 0 (the player owns Kenshi) once Steam answered.

Plan: the client sends its ticket in `Hello.ticket` (protocol 6); a server with Steam available
accepts the player when Steam validates the ticket (and the licence), with no browser step and no
per-server token. Steam OpenID stays as the fallback for servers without the Steam API. A Linux
server needs `libsteam_api.so` (Steamworks SDK redistributable, kept out of the repository) and
`steamclient.so` (SteamCMD).

## Security checks (2026-11-13, against the EU test server)

| Attempt | Result |
|---|---|
| Invented SteamID, no ticket | refused (browser login asked) |
| Invented SteamID, random ticket | refused |
| A real player's ticket presented with another SteamID | refused |
| A real player's ticket replayed with their SteamID | **was accepted**, now refused |
| Real player (Kenshi) | admitted in ~1.3 s, no browser |

The replay hole came from trusting `UserHasLicenseForApp`, which answers before Steam validates the
ticket. The server now waits for `ValidateAuthTicketResponse_t` (callback 143) and admits a player
only on `k_EAuthSessionResponseOK`; cancelled (6) or already used (7) tickets are refused. The client
cancels its ticket when it leaves a server and keeps a single live ticket.
`tools/testclient --ticket-hex` reproduces these attempts.
