# Reverse-engineering notes

Rules: only byte signatures, offsets and our own explanations. No decompiled code.

## Target build

| Field | Value |
|---|---|
| Install | Steam, `D:\SteamLibrary\steamapps\common\Kenshi` (clean reinstall 2026-11-12) |
| `kenshi_x64.exe` size | 36,718,592 bytes |
| MD5 | `8A03C256F0DA1555D9CCEB939B41530A` |
| SHA-256 | `A596AB4E407C67B58599C54FFB32DC1BF2B64510CDEBD3FA9359EF05A576AEB1` |
| `currentVersion.txt` | `Kenshi 1.0.68 - x64 (Newland)` |
| Toolchain of the game | MSVC 2010 (imports `MSVCP100.dll` / `MSVCR100.dll`) |

## Facts established

- Kenshi imports `OgreMain_x64.dll` (incl. `Ogre::Root::getSingletonPtr`, `addFrameListener`),
  `MyGUIEngine_x64.dll`, and `steam_api64.dll` (`SteamAPI_Init`, `SteamInternal_CreateInterface`, …).
- `Plugins_x64.cfg` lists OGRE plugins loaded at start (`PluginFolder=.\`).
