# World of Kenshi

An open-source multiplayer mod for [Kenshi](https://lofigames.com/): dedicated servers,
a server-specific character per player, and every player's actions replicated to the others.

> Status: **alpha 0.3**. Expect bugs.

## Features (alpha 0.3)

- Multiplayer button in Kenshi's main menu (no injector) and a server list.
- Steam login: the server checks the Steam account and Kenshi ownership. No browser, no extra account.
- Characters live on the server: position, skills, health, inventory, equipment, money and look are
  saved continuously. A new character starts where the Kenshi start you picked puts it.
- Players see each other with their names; other players **walk** with Kenshi's own animations and
  look like their character template.
- **Chat**: global, or local (`/l`) to players nearby, in a resizable Kenshi-style frame.
- An **Options > Multiplayer** tab in Kenshi's own Options window (chat opacity).
- **Server mod list**: a server can require the same mods in the same order; players are told what
  to change.
- Permanent death: a dead character is gone; the player makes a new one or leaves. Players can also
  delete their character on a server.
- Server-side time: no pause, no speed-up; the world keeps going while players are offline, and the
  game shows the server's day.
- Other players wear their exact face and body; their names show above their heads.
- **Shared world (early)**: each area is simulated by one player's game and its NPCs are shown to
  the players nearby (combat and trading with them are not synced yet).
- Hunger is saved; **Direct connect** for private servers.
- Dedicated server for Linux and Windows; anyone can host one (no central official server).

## Installing the client (players)

Download `WorldOfKenshi-alpha-0.3.zip` from the releases, extract it anywhere and run `install.bat`;
it finds your Kenshi folder (or asks for it). Start Kenshi from Steam and click **Multiplayer** in
the main menu. `uninstall.bat` restores Kenshi as it was. Requires Kenshi 1.0.68 (Steam).

In game: **Enter** opens the chat, **Enter** sends, **Escape** cancels; start a message with `/l` to
talk only to players nearby.

## Hosting a server

**Windows**: download `WorldOfKenshi-server-windows-alpha-0.3.zip`, set your server's name and the path
to Kenshi's `steam_api64.dll` in `start_server.bat`, open UDP 27800 and TCP 27801, and run it. The
server shows up in every player's server list.

**Linux**: `tools/vps/deploy.sh` builds and installs the server as a systemd service on a Debian or
Ubuntu machine.

Useful options (`wok_server --help` for all):

| Option | What it does |
|---|---|
| `--name`, `--description` | What players see in the server list |
| `--mods-file mods.txt` | Require the mods in this file (a copy of Kenshi's `data/__mods.list`), in order |
| `--default-spawn "The Hub"` | Town for new characters when the start's position is not used |
| `--force-spawn` | Always start new characters at the default town |
| `--announce http://HOST:27801` | Publish the server to a server list (`--master` hosts one) |
| `--steam-api path` | Steam login without a browser (Kenshi's `steam_api64.dll` or `libsteam_api.so`) |

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
