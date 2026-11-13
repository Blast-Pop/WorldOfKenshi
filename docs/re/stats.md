# Character stats (Kenshi 1.0.68)

Found with the Ctrl+Shift+F10 probe and by reading the game's stat accessor (RVA `0x884190`,
`(CharStats*, statId) -> float*`) and stat name function (RVA `0x888440`).

- `Character + 0x450` -> `CharStats` (same pointer also at `+0x600`). `Character + 0x2e8` -> `Inventory`,
  `+0x648` -> `CharBody`.
- Each skill is one `float` inside `CharStats`: the integer part is the level, the fraction is the
  progress to the next level. A new character has 1.0 in most skills.

| Id | Skill | CharStats offset |
|---:|---|---|
| 1 | Strength | 0x80 |
| 2 | Attack | 0x120 |
| 3 | Labouring | 0xe4 |
| 4 | Science | 0xe0 |
| 5 | Engineering | 0xcc |
| 6 | Robotics | 0xdc |
| 7 | Weapon Smithing | 0xd0 |
| 8 | Armour Smithing | 0xd4 |
| 9 | First Aid (also ids 14, 15 "Vet") | 0x98 |
| 10 | Thievery | 0xac |
| 11 | Turrets | 0x114 |
| 12 | Farming | 0xe8 |
| 13 | Cooking | 0xec |
| 16 | Stealth | 0xa4 |
| 17 | Athletics | 0x94 |
| 18 | Dexterity | 0x88 |
| 19 | Defence | 0x124 |
| 21 | Toughness | 0x90 |
| 22 | Assassination | 0xb8 |
| 23 | Swimming | 0xa8 |
| 24 | Perception | 0x8c |
| 25 | Katanas | 0xf8 |
| 26 | Sabres | 0xfc |
| 27 | Hackers | 0x100 |
| 28 | Heavy weapons | 0x108 |
| 29 | Blunt | 0x104 |
| 30 | Martial arts | 0x10c |
| 31 | (unnamed) | 0x9c |
| 32 | Dodging | 0xf0 |
| 34 | (unnamed) | 0x118 |
| 35 | Crossbows | 0x110 |
| 36 | Precision Shooting | 0xf4 |
| 37 | Lockpicking | 0xb0 |
| 38 | Crossbow Smithing | 0xd8 |

Id 20 (Weaponry) and 33 return a shared dummy, not a per-character value.
