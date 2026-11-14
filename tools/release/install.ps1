<#
.SYNOPSIS
  World of Kenshi player installer: copies wok_client.dll into Kenshi and registers it as a plugin.
  Run through install.bat (or uninstall.bat). Kenshi's plugin list is backed up first.
#>
param([switch]$Uninstall, [string]$KenshiDir = "")
$ErrorActionPreference = "Stop"

function Find-Kenshi {
    $candidates = @()
    $steam = (Get-ItemProperty -Path "HKCU:\Software\Valve\Steam" -ErrorAction SilentlyContinue).SteamPath
    if ($steam) {
        $candidates += Join-Path $steam "steamapps\common\Kenshi"
        $vdf = Join-Path $steam "steamapps\libraryfolders.vdf"
        if (Test-Path $vdf) {
            foreach ($m in [regex]::Matches((Get-Content $vdf -Raw), '"path"\s+"([^"]+)"')) {
                $candidates += Join-Path ($m.Groups[1].Value -replace '\\\\', '\') "steamapps\common\Kenshi"
            }
        }
    }
    foreach ($c in $candidates) { if (Test-Path (Join-Path $c "kenshi_x64.exe")) { return $c } }
    return $null
}

if (-not $KenshiDir) { $KenshiDir = Find-Kenshi }
if (-not $KenshiDir -or -not (Test-Path (Join-Path $KenshiDir "kenshi_x64.exe"))) {
    $KenshiDir = Read-Host "Kenshi folder not found. Paste the folder that contains kenshi_x64.exe"
}
if (-not (Test-Path (Join-Path $KenshiDir "kenshi_x64.exe"))) { throw "kenshi_x64.exe not found in $KenshiDir" }
if (Get-Process kenshi_x64 -ErrorAction SilentlyContinue) { throw "Close Kenshi first, then run this again." }

$cfgPath = Join-Path $KenshiDir "Plugins_x64.cfg"
$dllDest = Join-Path $KenshiDir "wok_client.dll"
$line = "Plugin=wok_client"

# Back up the plugin list before touching it.
$backupDir = Join-Path $KenshiDir ("WorldOfKenshi\backup-" + (Get-Date -Format "yyyyMMdd-HHmmss"))
New-Item -ItemType Directory -Force $backupDir | Out-Null
Copy-Item $cfgPath $backupDir

$lines = Get-Content $cfgPath
if ($Uninstall) {
    $lines | Where-Object { $_.Trim() -ne $line } | Set-Content -Encoding ascii $cfgPath
    if (Test-Path $dllDest) { Remove-Item $dllDest }
    Write-Host "World of Kenshi removed from $KenshiDir (your characters stay on the servers)."
    return
}

Copy-Item (Join-Path $PSScriptRoot "wok_client.dll") $dllDest -Force
if (-not ($lines | Where-Object { $_.Trim() -eq $line })) { ($lines + $line) | Set-Content -Encoding ascii $cfgPath }
Write-Host "World of Kenshi installed in $KenshiDir"
Write-Host "Start Kenshi from Steam and click Multiplayer in the main menu."
