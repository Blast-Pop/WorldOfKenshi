# Character layout (Kenshi 1.0.68)

Confirmed in game: snapshots of a selected `CharacterHuman` taken before/after walking (F8 probe).

| Offset | Type | Meaning | Evidence |
|---|---|---|---|
| `+0x48` | `float x, y, z` | **World position** (y = height) | (-50978.8, 1533.5, 2932.5) → (-51357.1, 1568.3, 2680.5) → (-50796.8, 1542.1, 2909.7) while walking |
| `+0xb0` | `float w, x, y, z` | **Orientation quaternion** (rotation about the vertical axis: x≈0, z≈0) | w 1.0 → -0.26, y 0.0 → 0.966 (unit length) |
| `+0x670` | `float x, y, z` | Copy of the position (internal use, not relied upon) | same values as `+0x48` |
| `+0x67c` | `float x, ?, z` | Facing direction vector (horizontal) | unit-length changes with heading |
| `+0x8d8` | `float x, y, z` | Another copy of the position | same values as `+0x48` |

Yaw in degrees = `2 * atan2(q.y, q.w)` converted to degrees, normalised to [0, 360).

Still to find (Task 7, step 3): spawning a character and ordering it to move.

## Identity and appearance (found 2026-11-13 with the Ctrl+Shift+F4 probe)

Selected character "Max", Wanderer start, Greenlander, with hair/beard from the "Ark Haircuts" mod:

| Offset | Type | Meaning | Observed |
|---|---|---|---|
| `+0x18` | VC10 `std::string` | **Character name** | `Max` |
| `+0x40` | `GameData*` | Character template (start) | `Wanderer` [`1533662-rebirth.mod`] |
| `+0x610` | VC10 `std::string` | Gender | `male` |
| `+0xb20` | VC10 `std::string` | Body mesh | `human_male.mesh` |
| `+0xb70` | `GameData*` | **Race** | `Greenlander` [`17-gamedata.quack`] |
| `+0xb80` | `GameData*` | Hair style | `ark_haircut06b` [`59453-Ark Haircuts.mod`] |
| `+0xbd0` | `GameData*` | Beard | `ark_beard03` [`59445-Ark Haircuts.mod`] |

GameData `+0x28` = display name, `+0x58` = FCS string ID. Everything is sent by string ID, so modded
content (here a modded haircut) travels like vanilla content.

## Real position and teleport (verified in game 2026-11-13)

- `Character + 0x640` -> movement object (`CharMovement`, base `AbstractMovementBase`). Its vtable slot
  `+0x40` returns a pointer to its position (`this + 0xc4`). Every frame Kenshi copies that position into
  `Character + 0x48` (code at exe+0x5cf3e9..0x5cf403), so writing `+0x48` has no lasting effect; writing
  `+0xc4` is also undone (the physics controller at `+0x320` drives it).
- **Teleport**: `CharMovement` vtable slot **`+0xc8`**, called as `(movement, const Vector3* position)`.
  It stores the position, pushes it to the physics controller (`+0x320`) and refreshes dependants.
  Verified with the Ctrl+Shift+F2 probe: the selected character jumped 50 units and stayed there.
- Other slots seen: `+0x30` set orientation (quaternion at `+0xd0`), `+0x38` turn towards a point,
  `+0x98` reset movement state.
