# World of Kenshi

An open-source multiplayer mod for [Kenshi](https://lofigames.com/): dedicated servers,
a server-specific character per player, and every player's actions replicated to the others.

> Status: early development. Milestone 1 — two players see each other walk in game.

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
