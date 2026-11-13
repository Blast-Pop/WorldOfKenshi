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

## Creating, giving and removing items (verified in game 2026-11-13)

- GameData lookup by string ID **and type**: RVA `0x6bfe60` `(GameWorld+0x20, const std::string*, int type)`.
  The type is at `GameData + 0x50` (weapons 2, armour 3, weapon manufacturer 51, weapon model 50,
  armour grade 47). RVA `0x6bfda0` did not find item records.
- Item factory RVA `0x580750` `(factory = *(GameWorld+0x4a0), GameData* data, hand*, GameData* p4,
  GameData* p5, int level, void*)`. A weapon is created with `data` = manufacturer, `p4` = weapon,
  `p5` = model (`Item + 0xc0`, `+0x40`, `+0xc8` of an existing weapon); other items with their own
  record and `p5` = grade/material. Handle: `{hand vtable from RVA 0x1e3a5f8, 0xb, 0, 0, 0}`.
- `Character` vtable `+0x168`: give an item `(Item*, bool)`; Kenshi equips it in a free matching
  slot (a second weapon goes to `back` = Weapon II) or puts it in `main`.
- `Inventory` vtable `+0x30`: remove and destroy an item `(Item*, int qty = -1, bool)`;
  `+0x38` removes without destroying; `Character` vtable `+0x178` checks an item count.
- Stack quantity: `Item + 0x12c` (int).
- World of Kenshi saves the inventory as text lines
  `section, template id, type, aux1 id, type, aux2 id, type, quantity` and restores it by removing
  the start kit, then creating and giving every saved item.
