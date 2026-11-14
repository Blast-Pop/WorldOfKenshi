# AI orders (Kenshi 1.0.68) — in progress

Goal: make remote players' avatars walk/run with their animations instead of following by teleport.

- AI task classes have RTTI names `Task_*` (93 of them); a plain move is `Task_Move` (vtable
  `0x1416bdd88`, constructor RVA `0x332040`).
- Task factory RVA `0x32ebd0`: `(context, int type, hand* target, int, float, const Vector3* pos, int, int)`
  → `Task*`; types 1, 29, 40 and 67 build a `Task_Move`. The factory stores `target` at task `+0x18`,
  the first int at `+0x8`, the float at `+0x30`, the position at `+0x58`, the last ints at `+0x64/+0x68`.
- When a save is loaded, the game gives a restored task with: `context = *(*(character + 0x650) + 0x2a8)`,
  then `(*(character + 0x648))->vtable[0x18](task)`.
- Tried: type 1 through that path. The call succeeds, but neither the player's character nor a
  spawned avatar walks (the player's moves ~1 unit then stops). Missing: the real "order" entry point
  used by a right-click move, or the avatars' AI being active. Avatars follow by teleport meanwhile.
- `GameWorld::destroy` RVA `0x799af0` `(GameWorld*, RootObject*, int, const char* reason)` removes an
  object; used to remove the avatar of a player who left (verified).
