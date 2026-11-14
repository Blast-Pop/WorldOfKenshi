# World of Kenshi

An open-source multiplayer mod for [Kenshi](https://lofigames.com/): dedicated servers,
a server-specific character per player, and every player's actions replicated to the others.

> Status: **alpha 0.1**. Expect bugs.

## Features (alpha 0.1)

- Multiplayer button in Kenshi's main menu (no injector) and a server list.
- Steam login: the server checks the Steam account and Kenshi ownership. No browser, no extra account.
- Characters live on the server: position, skills, health, inventory, equipment, money and look are
  saved continuously.
- Players see each other, with their names (other players still move by short teleports).
- Permanent death: a dead character is gone; the player makes a new one or leaves.
- Server-side time: no pause, no speed-up; the world keeps going while players are offline.
- Dedicated server for Linux and Windows; anyone can host one (no central official server).

## Installing the client (players)

Download the release zip, extract it anywhere and run `install.bat`; it asks for (or finds) your
Kenshi folder. Start Kenshi from Steam and click **Multiplayer** in the main menu.
`uninstall.bat` restores Kenshi as it was.

## How it works (short)

- **Client mod** (`wok_client.dll`): a C++ DLL that Kenshi loads as an OGRE plugin. It reads the
  local player's state and shows other players.
- **Dedicated server** (`wok_server`): a standalone C++ program (Windows & Linux) that holds the
  authoritative world state and relays players to each other.
- **Shared** (`wok_shared`): wire protocol and networking (Valve GameNetworkingSockets).

See `docs/superpowers/specs/` for the design and `docs/superpowers/plans/` for the roadmap.

## What this repository contains — and does not

This repository contains **only our own code**. It never contains Kenshi's code (decompiled or
otherwise) or any game files. Reverse-engineering notes in `docs/re/` are limited to byte
signatures, offsets and our own explanations. Every player and server host uses their own
Steam copy of Kenshi.

## Building

Requirements: CMake ≥ 3.28, Visual Studio 2026 Build Tools on Windows (Ninja + GCC/Clang on Linux), and vcpkg cloned into `./vcpkg` at the commit pinned in `vcpkg.json` (`builtin-baseline`), then bootstrapped.

```sh
cmake --preset windows-debug        # or linux-release
cmake --build --preset windows-debug
ctest --preset windows-debug
```

## License

MIT — see `LICENSE`. Kenshi is © Lo-Fi Games; this project is not affiliated with them.

---

Made with the 🧠 of Claude, with all the ❤️ of Blast-Pop.
