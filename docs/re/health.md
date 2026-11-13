# Character health (Kenshi 1.0.68)

Found with the Ctrl+Shift+F10 probe; verified in game 2026-11-13 (saved values shown back in the
health panel).

- `MedicalSystem` is embedded in the character at `+0x458`. Blood: float at `character + 0x4c8`.
- Body parts: Kenshi array of `HealthPartStatus*` at `character + 0x5e8` (vtable, `u32 count` at
  `+0x5f0`, data at `+0x5f8`); 7 parts in this order: head, chest, stomach, left arm, right arm,
  left leg, right leg (`+0x8` kind: 3 head, 2 arm, 1 leg; `+0x20` side 1/2).
- `HealthPartStatus + 0x40` and `+ 0x54`: two floats, both 100 on a healthy part; `+0x40` is the
  value shown in the health panel. `+0x38` looks like the part's base value (80/140/40).
- World of Kenshi saves blood and both part fields through the skills channel (`StatsUpdate`) with
  ids Kenshi does not use: 48 blood, 49..55 parts `+0x40`, 56..62 parts `+0x54`.

Not covered yet: hunger, bleeding/bandage state, lost limbs (robotic limbs are items).
