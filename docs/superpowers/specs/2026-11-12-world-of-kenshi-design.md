# World of Kenshi — Design document

*Date: 2026-11-12 · Author: Aeglen (vision), Claude (writing)*
*Source of the vision: `docs/vision-flowchart.pdf`*

## 1. Intent

Turn Kenshi into an MMO on dedicated servers. Same game, same world, same rules, shared by many players. Each player joins a server with a character that belongs to that server, and builds a life, a base and a faction there. What a player does is replicated to every other player concerned.

The project starts from scratch. The existing multiplayer mods were tried and judged insufficient. Our code (the mod and the server) is open source on GitHub, with the aim of applying to Claude's OSS program.

## 2. Goals

- Dedicated servers aiming for **up to 250 connected players**.
- **Vanilla Kenshi**: no mandatory gameplay change. The mod adds networking, not a new game.
- **Server-side characters only (firm rule)**: the server stores players' characters in its database (keyed by SteamID). On connect, a player who has a character **automatically loads** the server's world with it; otherwise Kenshi's **character creation** opens and the result is sent to the server. **A local save is never usable in multiplayer**, neither to play nor to test (no "dev" mode around the rule). Once connected, Continue / New Game / Load / Import are hidden.
- **Mod support (essential for the community)**: a server can run data mods (FCS). See §7.6. Not in M1.
- **Joining and starting point (specified 2026-11-13)**: on Connect, the client sends the player's identity; if a character exists, the server's game starts directly with it. Otherwise the player **freely picks a Kenshi start** (Wanderer, Freedom Seekers, etc.; decided 2026-11-13, it changes little over time), then makes a character in Kenshi's editor; the result is sent to the server. The server defines **several spawn points in popular towns**, one of them the **default**. The world being large and time running at x1, **the player picks a starting town** in a menu (default preselected); the character appears there.
- **Fixed time imposed by the server**: no player can speed up time (Kenshi's x2/x5 speeds blocked, always x1). The world's time (day/night cycle, date) comes from the server and is the same for everyone. The pause is also disabled in multiplayer: opening a menu or the inventory does not freeze the world. The server may set the game time rate in its configuration (admins only).
- **Temporary corpses (5 minutes)**: after death, a body (NPC or player) stays "fresh" for **5 minutes**: animals can eat it, looters search it, NPCs react. Then it **disappears**. The server is authoritative on the time of death and the removal (the same for all clients, surviving a restart within the window). *Claude's choice, to confirm:* what is left in the body's inventory disappears with it. Not in M1.
- **Rust-style persistence**: a disconnected player leaves their character, recruits and base in the world, vulnerable. A protection mechanism will be designed later.
- **Replication** of each player's actions (movement, combat, building, inventory, factions) to the other clients concerned.
- Clean, publishable code: README, license, reproducible build.

## 3. Non-goals (for now)

- Offline protection of bases: to be designed later.
- New gameplay content.
- Mods that inject code (DLL plugins like RE_Kenshi): not supported at first.
- Advanced anti-cheat: V1 is limited to plausibility checks.

## 4. Anti-goals (a failure even if it works)

- Publishing Kenshi's code, decompiled or copied, or game files. **Only our code is published.**
- A world that diverges between players: two players in the same zone must never see two different realities for long.
- Depending on KenshiLib, RE_Kenshi or the existing "Kenshi Online" projects.

## 5. Constraints

- Kenshi (Steam, `kenshi_x64.exe`, OGRE engine, Windows x64). The base game has no network API.
- The developers tolerate modding. We inject a DLL and hook game functions found by reverse engineering (with the REA tool).
- **Reverse-engineering rule**: REA is used to *understand* (addresses, structures, behaviour). We *write* our own code. Addresses and structure definitions in the repository are accepted, as in SKSE.
- Kenshi simulates large numbers of active entities poorly. No client can simulate a 250-player world.
- Every player, and every host of headless instances, owns their own Steam copy of the game.

## 6. Player journey (from the vision flowchart)

1. The player starts Kenshi. The mod checks with Steam that the player owns the game; otherwise they cannot play.
2. The mod is active. The main menu shows a **"Multiplayer"** button.
3. The button opens the **server list**. The player picks a server.
4. The mod asks the server: "Can I join?"
   - **No**: the reason is shown (missing mod or version, server full, banned).
   - **Yes**: authentication (Steam), then the server character is loaded or created.
5. The server sends the world state (the cells around the player). The mod applies it locally.
6. The client sends back a summary (hash) of the applied state: "Is this right?"
   - **Yes**: the player plays.
   - **No**: a resynchronisation is attempted. After several failures the player is kicked with "Connection Timeout".

## 7. Architecture (decisions taken)

### 7.1 Components
- **Client mod**: C++ DLL loaded into `kenshi_x64.exe`. It hooks the game, provides the Multiplayer interface and the server list, and applies the received state.
- **Dedicated server**: standalone C++ program, not Kenshi. It is authoritative on persistent state: accounts, characters, inventories, buildings, factions, positions. It splits the world into a grid, assigns owners and relays messages. It runs on Windows and Linux.
- **Shared code**: protocol, serialisation and grid definitions, common to client and server.

### 7.2 Grid world and streaming
- The map is split into fixed-size **cells (chunks)**.
- Each client only receives the cells around it (interest radius) and their updates.

### 7.3 Cell owner
- Each occupied cell has **one owner**: its Kenshi instance simulates the cell (NPC AI, combat, damage, needs).
- The owner sends the simulation state to the server, which forwards it to the other players in the cell. Those players display it without simulating the NPCs themselves.
- Each player controls their own characters. Their actions go through the server and are resolved by the owner.
- **Succession by ping**: for each cell the server keeps a **ranking of the present clients by best ping**. The first is the owner, the next ones its successors, in order. When the owner leaves, the next takes over immediately from the last known state, with no election.
- **Empty cell**: with no player the cell is frozen. The server stores its state and nothing moves until someone comes back (see §7.7 for the catch-up on thaw).
- The server checks the plausibility of the data it receives: movement speed, damage consistent with stats, items appearing from nowhere. On a mismatch it resynchronises.

### 7.4 Planned evolution: headless instances
The "owner" role is **generic** in the protocol. Later the server may start Kenshi instances **without display** (rendering, sound and UI disabled by our DLL) that become cell owners. This takes authority away from clients, which solves most cheating, and lets empty cells keep living. The cost: a powerful Windows machine and a copy of the game on the host side. **Not in V1**, but the architecture must not prevent it.

### 7.5 Technical choices (Claude's, not contested by Aeglen)
- C++20 and CMake, for client and server.
- Networking: **GameNetworkingSockets** (Valve, open source).
- Authentication: Steam (OpenID login, then a per-server token).
- License: MIT.
- Public GitHub repository.

### 7.6 Mods
Mods keep the community alive: a modded server must be a normal case, not an exception.
- **The server imposes the mod list and load order** (like modded ARK/Rust servers). All clients of a server have exactly the same content, otherwise the world diverges.
- **Check on connect**: the client sends its list (identifier + hash of each `.mod` file, in order). Any difference → refusal with the reason "Missing / different mods: X, Y" (the "dependency" case of the vision flowchart).
- **Steam Workshop mods are identified by their Workshop ID**; later, automatic download when joining a server. Non-Workshop mods are identified by name + hash.
- **Target: data mods** (FCS: items, races, factions, buildings, armour…). They go through the normal engine, so replication covers them like base content.
- **DLL plugins** (injected code): not supported at first, since we cannot guarantee they stay in sync.
- **Architecture rule, from now on**: the protocol designates all content (item, race, faction, building…) by its **FCS data identifier** (`stringID`), never by a hard-coded numeric index or a pointer. An item added by a mod thus travels like a base-game item.

### 7.7 Continuous server-side save (requested 2026-11-13)
- The server is the players' **only** save; the client never writes anything to disk in multiplayer (pause menu: no Save/Load/New Game, its Exit Game button becomes **Disconnect**).
- The client sends **changes as they happen**: position/state at 10 Hz, and every change of inventory, skills, money, health and squad as soon as it happens.
- The server applies these changes **in memory immediately** and writes them to SQLite (WAL mode, crash-safe) in **small batches every ~2 s**, and **immediately** on disconnect.
- **Client crash**: nothing is lost beyond the last seconds; the character stays in the world (Rust-style rule) and the player finds it again on reconnect.
- **Server crash**: at worst the last ~2 s; the database stays consistent (WAL) and timestamped database backups are made regularly.
- **Server time (Aeglen, 2026-11-13)**: a player can never control the server's time. In a multiplayer world the pause and speed controls (buttons and keys) are disabled; time always runs at normal speed.
- **Skills (done 2026-11-13)**: the client sends changed skills once a second (`StatsUpdate`); the server keeps them in memory and writes them every 2 s and on disconnect; a returning player gets them back in `CharacterData` (docs/re/stats.md). Health and money travel on the same channel (docs/re/health.md).
- **World time (decided 2026-11-13)**: the server owns the world clock. It advances from real elapsed time at Kenshi's normal rate, even with nobody online, and is saved in the database. Clients never set it: they align their game clock to the server's every few seconds.
- **Frozen zones (decided 2026-11-13)**: a zone nobody simulates is frozen; when it thaws, the elapsed time is applied in summary (crops grow, food rots, wounded NPCs heal, traders restock) instead of being replayed.

### 7.8 Player chat (requested 2026-11-14)
- Two channels, both relayed by the server (never peer to peer):
  - **Global**: every player on the server.
  - **Local**: players within a radius of the speaker (proximity chat, radius set by the server; the server measures it from the positions it already receives).
- An in-game chat box (MyGUI, our own widgets): a key opens the input line, the player picks the channel (e.g. `/g` and `/l`, or a toggle), messages show the sender's character name and fade out after a while.
- The server limits message length and rate (anti-spam), strips control characters, and logs messages; the server owner can mute or kick.
- Messages are not saved in the character data; only the server log keeps them.

## 8. First milestone (M1): "it works"

**Two players connect to the same server and see each other walk in game.**

What M1 implies at minimum:
- The DLL is loaded into Kenshi.
- A minimal server accepts two connections. Steam authentication may be simplified or disabled in development.
- Each client sends its character's position and orientation.
- Each client spawns and moves a character representing the other player, with a walking animation.
- A Multiplayer button and an IP field are enough. The real server list comes later.

Not in M1: cell owners, NPCs, combat, inventory, persistence, grid.

## 9. Left to the builder

- The injection method: proxy DLL (e.g. `dinput8.dll` or a DLL Kenshi already loads) or a launcher; prefer what works with a Steam launch. (Chosen: an OGRE plugin listed in `Plugins_x64.cfg`.)
- The hooking library (MinHook or equivalent).
- Cell size, interest radius and send rate.
- The serialisation format.
- Server-side persistence storage (SQLite by default).
- Repository layout and CI.

## 10. Open questions (to decide later)

- The protection mechanism for offline players and bases.
- How to replicate factions and relations between player factions.
- Automatic download of missing Workshop mods when joining (exact mechanism).
- ~~Server discovery: master list or Steam listing~~ — decided: a master server list (`--master` / `--announce`).
- ~~Behaviour of a cell that thaws after a long absence~~ — decided in §7.7 (summary catch-up).
