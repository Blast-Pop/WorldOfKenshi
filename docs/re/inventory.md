# Character inventory (Kenshi 1.0.68)

Found with the Ctrl+Shift+F10 probe (client/src/game/re_probe.cpp).

- `Character + 0x2e8` -> `Inventory`. `Inventory + 0x80/+0x88` point back to the owner.
- `Inventory + 0x68` is one of Kenshi's arrays: vtable, `u32 count` at `+0x70`, `u32 capacity`
  at `+0x74`, `InventorySection**` at `+0x78`. A human has 10 sections.
- `InventorySection`: name (`std::string`) at `+0x08` (`hip`, `back`, `boots`, `head`,
  `backpack_attach`, `shirt`, `armour`, `legs`, `belt`, `main`), grid size at `+0x30`
  (`u32 width, u32 height`, e.g. `main` 8x10), and a `std::vector` of 16-byte entries at
  `+0x40` (begin) / `+0x48` (end) / `+0x50` (capacity); each entry starts with the `Item*`.
- `Item` (most-derived classes seen: `Sword`, `Armour`): display name `std::string` at `+0x18`,
  item template `GameData*` at `+0x40` (string ID at GameData `+0x58`, e.g. "52295-rebirth.mod"
  for an Iron Club), `GameData*` at `+0xc0` (weapon manufacturer) and `+0xc8` (weapon model or
  armour grade), name of the section holding it at `+0xe8`.

Still unknown: the stack quantity and condition fields, and the game's own functions that create
an item from a GameData and put it in a section (needed to restore an inventory).

## Creating items (in progress, 2026-11-13)

- Item factory: RVA `0x580750`, `(factory = GameWorld+0x4a0, GameData* data, hand*, GameData* p4,
  GameData* p5, int level, void*) -> Item*`. Switches on `GameData + 0x50` (type): it returns null
  for type 2, and for type 0x33 builds a weapon (`Sword`) from `p4` (weapon) + `data` + `p5`
  ("weapon models"), so a weapon is created from its manufacturer/model data, not from the type-2
  weapon record alone. To check next: the exact roles of `data`/`p4`/`p5` for weapons.
- The game passes a fresh handle `{hand vtable, 0xb, 0, 0, 0}`; the hand vtable can be read from the
  global empty hand at RVA `0x1e3a5f8`.
- Character vtable slot `+0x168`: give an item `(Character*, Item*, bool)`; a dialog reward uses it.
- GameData lookup RVA `0x6bfda0` `(GameWorld+0x20, const std::string*, int type)`: the data is
  indexed by type. The held Iron Club record is type 2, but looking up "52295-rebirth.mod" with
  type 2 returned null: to investigate (maybe items need the type-specific lookup RVA `0x6bfe60`).
- Ctrl+Shift+F2 probe: tries to create a copy of the held weapon and give it.
