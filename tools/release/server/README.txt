World of Kenshi - dedicated server (alpha 0.2), Windows
=======================================================

1. Open start_server.bat in a text editor and set NAME, DESCRIPTION and the path to
   Kenshi's steam_api64.dll (players then log in with Steam, no browser).
2. Open these ports on your router / firewall to this PC:
     UDP 27800  (game)
     TCP 27801  (fallback browser login and server info)
3. Double-click start_server.bat. The server appears in every player's server list.
   Private server: set PRIVATE=1; it stays out of the list and players use "Direct connect"
   in the Multiplayer window with your address (e.g. 203.0.113.5:27800).

Mods: copy your Kenshi data\__mods.list next to the server as mods.txt. Players must then
run exactly these mods, in the same order; anyone else is told what to change.

The world (characters, time) is saved in world.sqlite, continuously. Back it up from time
to time. Linux servers: see tools/vps in the source code.

Source code and help: https://github.com/Blast-Pop/WorldOfKenshi
