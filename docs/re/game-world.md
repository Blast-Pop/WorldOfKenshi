# Game world and player (Kenshi 1.0.68)

All addresses are RVAs in `kenshi_x64.exe` (image base 0x140000000 on disk; ASLR applies at runtime,
so always add the runtime module base). Found 2026-11-12 with RTTI + our own xref scanner, then
confirmed in game with the F8 probe (`client/src/game/re_probe.cpp`).

## GameWorld

- Class `GameWorld`, vtable RVA `0x1722608`.
- It is a **static global object** (not a pointer) at RVA **`0x2134110`**: a dynamic initializer
  does `lea rcx, [GameWorld]` then jumps to the constructor.
- Probe check: `*(base + 0x2134110) == base + 0x1722608` → "GameWorld vtable OK".

## PlayerInterface

- Class `PlayerInterface`, vtable RVA `0x171a2b8`, object size `0x2f8` (allocation size seen at its `new`).
- Stored at **`GameWorld + 0x580`** (pointer), assigned during world initialisation.
- `PlayerInterface + 0x2a8` → `Platoon*` (squad of the current selection).
- `PlayerInterface + 0x2c0` → pointer to an array of `Character*`; element `[0]` is the **selected
  character**. The element count is not yet identified (TODO), so the client validates the
  pointer with the vtable check below and caches the last valid character.

## Known vtables (RVA)

| Class | vtable |
|---|---|
| CharacterHuman | `0x16f2848` |
| Character | `0x16f9eb8` |
| CharacterAnimal | `0x16f2208` |
| Platoon | `0x1718808` |
| ActivePlatoon | `0x16e2e08` |
| RootObject | `0x16ef338` |
| GameWorld | `0x1722608` |
| PlayerInterface | `0x171a2b8` |

## Game speed (verified in game 2026-11-13)

- `GameWorld + 0x700` (float): game speed. 0 = paused, 1 = normal, 2 and 5 = fast. Found with the
  Ctrl+Shift+F11 probe, which clicks the speed buttons and diffs the GameWorld object.
- Speed controls live in `Kenshi_MainPanel.layout`: `SpeedButtonsPanel`, `TimeSpeedButton1..4`
  (pause, x1, x2, x5), `SpeedArrowLeft/Right`, and `PausedPanel`.
- World of Kenshi hides those controls in a multiplayer world and clicks `TimeSpeedButton2`
  whenever the speed is not 1 (keyboard shortcuts included).

## World clock and menu pause (verified in game 2026-11-13)

- Time-of-day object: global pointer at RVA `0x21303d0`. `+0` hours and `+4` minutes (ints, display),
  `+8` day (int, 1 on "Day 1"), `+0x18` SkyX, `+0x20` SkyX time controller whose `+0x1c` is the hour
  of the day (float 0-24). Per-frame update: RVA `0x66f190`; the day increments when the hour wraps.
- Time rate: `gameSpeed * 0.009166666` game hours per real second (one game day ~ 43.6 min).
- `GameWorld + 0x8b9` (byte): set while a menu (Escape) pauses the world; the time update multiplies
  by 0 then. World of Kenshi clears it in a multiplayer world.
- World of Kenshi sets day + hour from the server's clock (`WorldTime`, every 5 s) when they differ
  by more than 3 game minutes.
- Do not write `GameWorld + 0x733c` (1 during normal play; clearing it closes the game).
  `GameWorld + 0x132c` is a round-robin counter.
