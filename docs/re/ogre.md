# OGRE in Kenshi

- `OgreMain_x64.dll` identifies itself as **OGRE 2.0 "Tindalos"** (string in the DLL), built by Lo-Fi
  Games with MSVC 2010. Export directory timestamp 1554381043.
- Kenshi loads every `Plugin=` entry of `Plugins_x64.cfg` and calls the exported
  `dllStartPlugin` during `Ogre::Root` construction. Verified 2026-11-12: `wok_client.dll` loads this way.

## `Ogre::FrameListener` vtable (from the exported `??_7FrameListener@Ogre@@6B@`)

Kenshi's build has **one more virtual than stock OGRE**:

| Slot | Function | Default body |
|---|---|---|
| 0 | `bool frameStarted(const FrameEvent&)` | returns true |
| 1 | `bool frameRenderingQueued(const FrameEvent&)` | returns true |
| 2 | *(Kenshi-specific, returns void)* | empty |
| 3 | `bool frameEnded(const FrameEvent&)` | returns true |
| 4 | scalar deleting destructor | — |

Our `ogre_abi.hpp` reproduces this exact order. `FrameEvent` is `{ float timeSinceLastEvent; float timeSinceLastFrame; }`.

## Entry points resolved at runtime (GetProcAddress on OgreMain_x64.dll)

- `?getSingletonPtr@Root@Ogre@@SAPEAV12@XZ` → `Ogre::Root*`
- `?addFrameListener@Root@Ogre@@QEAAXPEAVFrameListener@2@@Z` → `void (Root*, FrameListener*)`
