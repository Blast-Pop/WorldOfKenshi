# Spawning characters (Kenshi 1.0.68) — work in progress

Our own notes (offsets and roles only). RVAs in `kenshi_x64.exe`.

## Objects

- **RootObjectFactory**: pointer stored at `GameWorld + 0x4a0` (assigned during world initialisation,
  just before the PlayerInterface is created).
- **Game data manager**: inside `GameWorld + 0x20`; the game looks templates up by FCS string ID
  (e.g. `"random character base"`), which matches our "content by stringID" wire rule.
- **ActivePlatoon** (squad container) has `+0x78` -> **Platoon**; the Platoon's vtable slot `0x58`
  returns its **Faction***. Squad members: count at ActivePlatoon `+0x58`, array at `+0x60`.

## Creating one character: RVA `0x5836e0`

Called by squad spawning code once per member. Arguments, in order:

1. RootObjectFactory*
2. Faction* of the new character
3. pointer to the world position (3 floats)
4. ActivePlatoon* that receives the character
5. character template (GameData*) — **0 picks the game's default "random character base"**
6. home building (0 = none)
7. float, 1.0 in all observed calls

Returns the new Character* (or null). Callers then set the member's squad role with RVA `0x6215c0`
(0 = member, 2 = leader, as observed).

Prologue bytes `40 55 53` are checked before calling, as a cheap version guard.

## Verified in game (2026-11-12)

Selected character `+0x658` -> its ActivePlatoon. Two calls each created a `CharacterHuman` 40 units
away in the player's squad, no crash.

## Status

Ctrl+Shift+F7 dev key (`re_probe.cpp`): spawns a default character 40 units from the selected character, into
that character's own squad — an experiment to validate the call, not the final avatar behaviour.
Next: own squad + neutral faction for remote players, AI off, move orders.
