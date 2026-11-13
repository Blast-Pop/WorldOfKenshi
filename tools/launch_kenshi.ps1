# Dev helper: closes Kenshi, installs the freshly built client, starts Kenshi through Steam and
# presses OK on its launcher window (video settings dialog), so a test run needs no manual step.
#   powershell -ExecutionPolicy Bypass -File tools/launch_kenshi.ps1 -KenshiDir "D:\...\Kenshi"
param(
    [Parameter(Mandatory = $true)][string]$KenshiDir,
    [int]$LauncherTimeoutSec = 60
)
$ErrorActionPreference = 'Stop'

Add-Type @'
using System;
using System.Runtime.InteropServices;
using System.Text;
public static class WokWin {
    public delegate bool EnumProc(IntPtr hwnd, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr parent, EnumProc cb, IntPtr lParam);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetWindowText(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll", CharSet = CharSet.Unicode)] public static extern int GetClassName(IntPtr h, StringBuilder s, int n);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr h);
    [DllImport("user32.dll")] public static extern IntPtr SendMessage(IntPtr h, uint msg, IntPtr w, IntPtr l);
    public static string Text(IntPtr h) { var s = new StringBuilder(256); GetWindowText(h, s, 256); return s.ToString(); }
    public static string Cls(IntPtr h) { var s = new StringBuilder(256); GetClassName(h, s, 256); return s.ToString(); }

    public static IntPtr FindTop(string titlePrefix) {
        IntPtr found = IntPtr.Zero;
        EnumWindows((h, l) => {
            if (IsWindowVisible(h) && Text(h).StartsWith(titlePrefix)) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    public static IntPtr FindButton(IntPtr parent, string caption) {
        IntPtr found = IntPtr.Zero;
        EnumChildWindows(parent, (h, l) => {
            if (Cls(h) == "Button" && Text(h).Replace("&", "") == caption) { found = h; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
}
'@

# 1. Close a running Kenshi (the DLL is locked while the game runs).
$running = Get-Process kenshi_x64 -ErrorAction SilentlyContinue
if ($running) {
    Write-Host "Closing Kenshi..."
    $running | Stop-Process -Force
    $running | Wait-Process -Timeout 30 -ErrorAction SilentlyContinue
    Start-Sleep -Seconds 5   # Steam needs a moment to notice the game has stopped
}

# 2. Install the client build.
& (Join-Path $PSScriptRoot 'install.ps1') -KenshiDir $KenshiDir

# 3. Start Kenshi through Steam (keeps the Steam overlay and authentication), then press OK on the
#    launcher dialog. Steam sometimes ignores the request right after the game closed: retry.
for ($attempt = 1; $attempt -le 3; $attempt++) {
    Start-Process 'steam://rungameid/233860'
    $deadline = (Get-Date).AddSeconds($LauncherTimeoutSec / 3)
    while ((Get-Date) -lt $deadline) {
        $win = [WokWin]::FindTop('Kenshi 1.0')
        if ($win -ne [IntPtr]::Zero) {
            $ok = [WokWin]::FindButton($win, 'OK')
            if ($ok -ne [IntPtr]::Zero) {
                Start-Sleep -Milliseconds 500   # let the dialog finish initialising
                [void][WokWin]::SendMessage($ok, 0x00F5, [IntPtr]::Zero, [IntPtr]::Zero)  # BM_CLICK
                Write-Host "Launcher: OK pressed ($([WokWin]::Text($win)))"
                exit 0
            }
        }
        Start-Sleep -Milliseconds 300
    }
    Write-Host "Launcher not seen (attempt $attempt), asking Steam again..."
}
Write-Warning "Launcher window not found"
exit 1
