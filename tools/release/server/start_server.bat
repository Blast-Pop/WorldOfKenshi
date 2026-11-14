@echo off
rem World of Kenshi dedicated server - edit the settings below, then double-click this file.
set NAME=My World of Kenshi server
set DESCRIPTION=A World of Kenshi server
rem Kenshi's own steam_api64.dll lets players log in with Steam (no browser). Change the path if needed.
set STEAM_API=C:\Program Files (x86)\Steam\steamapps\common\Kenshi\steam_api64.dll
rem Optional: copy your Kenshi data\__mods.list next to this file as mods.txt to require the same mods.
set MODS=
if exist "%~dp0mods.txt" set MODS=--mods-file "%~dp0mods.txt"

"%~dp0wok_server.exe" --db "%~dp0world.sqlite" --name "%NAME%" --description "%DESCRIPTION%" ^
  --spawn-file "%~dp0towns.txt" --default-spawn "The Hub" --steam-api "%STEAM_API%" ^
  --announce http://95.179.253.252:27801 %MODS%
pause
