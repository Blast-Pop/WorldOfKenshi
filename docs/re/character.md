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
