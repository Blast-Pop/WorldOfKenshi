<#
.SYNOPSIS
  Installs (or removes) the World of Kenshi client into a Kenshi folder.
.EXAMPLE
  .\tools\install.ps1 -KenshiDir "D:\SteamLibrary\steamapps\common\Kenshi" -BuildDir build\windows-debug
  .\tools\install.ps1 -KenshiDir "D:\SteamLibrary\steamapps\common\Kenshi" -Uninstall
#>
param(
    [Parameter(Mandatory = $true)][string]$KenshiDir,
    [string]$BuildDir = "build\windows-debug",
    [string]$Config = "Debug",
    [string]$BackupRoot = "D:\Backups",
    [switch]$Uninstall
)
$ErrorActionPreference = "Stop"

$cfgPath = Join-Path $KenshiDir "Plugins_x64.cfg"
$dllDest = Join-Path $KenshiDir "wok_client.dll"
$line = "Plugin=wok_client"
if (-not (Test-Path (Join-Path $KenshiDir "kenshi_x64.exe"))) { throw "kenshi_x64.exe not found in $KenshiDir" }

# Always back up the plugin list before touching it.
$stamp = Get-Date -Format "yyyyMMdd-HHmmss"
$backupDir = Join-Path $BackupRoot "Kenshi-cfg-$stamp"
New-Item -ItemType Directory -Force $backupDir | Out-Null
Copy-Item $cfgPath $backupDir
Write-Host "Backed up Plugins_x64.cfg to $backupDir"

$lines = Get-Content $cfgPath
if ($Uninstall) {
    $lines | Where-Object { $_.Trim() -ne $line } | Set-Content -Encoding ascii $cfgPath
    if (Test-Path $dllDest) { Remove-Item $dllDest }
    Write-Host "World of Kenshi removed."
    return
}

$dllSrc = Join-Path $BuildDir "client\$Config\wok_client.dll"
if (-not (Test-Path $dllSrc)) { throw "Build output not found: $dllSrc" }
Copy-Item $dllSrc $dllDest -Force
if (-not ($lines | Where-Object { $_.Trim() -eq $line })) {
    ($lines + $line) | Set-Content -Encoding ascii $cfgPath
}
Write-Host "World of Kenshi installed: $dllDest"
