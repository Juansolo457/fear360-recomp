# Unattended check: launch our own windowed fear.exe with keyboard-as-controller (--mnk_mode),
# keep it in front, tap a key at an interval (Space = A: through "Press Start", the profile
# prompt, New game, difficulty and the intro), take screenshots on a schedule, then stop it.
# Its own profile and cache; it never touches a copy the user started.
#   pwsh -NoProfile -File tools/autoplay.ps1 -Nx -Seconds 150 -Extra '--nx_renderer native ...' -Tag packon
[CmdletBinding()]
param(
    [ValidateRange(10,900)][int]$Seconds = 150,
    [ValidateRange(1,60)][int]$KeyEvery = 2,
    [string]$Key = 'Space',
    [ValidateRange(0,900)][int]$KeysUntil = 0,      # stop tapping after this many seconds (0 = never)
    [ValidateRange(1,120)][int]$CaptureEvery = 15,
    [string]$Tag = 'run',
    [string]$Extra = '',
    [switch]$Nx,
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
Add-Type @'
using System; using System.Runtime.InteropServices;
public static class AutoInput {
  [StructLayout(LayoutKind.Sequential)] public struct KEYBDINPUT { public ushort wVk, wScan; public uint dwFlags, time; public IntPtr dwExtraInfo; }
  [StructLayout(LayoutKind.Sequential)] public struct INPUT { public uint type; public KEYBDINPUT ki; public long pad; }
  [DllImport("user32.dll")] public static extern uint SendInput(uint n, INPUT[] inputs, int size);
  [DllImport("user32.dll")] public static extern uint MapVirtualKey(uint code, uint mapType);
  [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
  [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
  [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h, int cmd);
  public static void Tap(ushort vk) {
    ushort scan = (ushort)MapVirtualKey(vk, 0);
    var down = new INPUT { type = 1, ki = new KEYBDINPUT { wVk = vk, wScan = scan } };
    var up = new INPUT { type = 1, ki = new KEYBDINPUT { wVk = vk, wScan = scan, dwFlags = 2 } };
    SendInput(1, new[] { down }, Marshal.SizeOf(typeof(INPUT)));
    System.Threading.Thread.Sleep(80);
    SendInput(1, new[] { up }, Marshal.SizeOf(typeof(INPUT)));
  }
}
'@
$vk = @{ Space = 0x20; Return = 0x0D; Escape = 0x1B; Up = 0x26; Down = 0x28; Left = 0x25; Right = 0x27; W = 0x57; S = 0x53 }[$Key]
if (!$vk) { throw "Unknown key $Key" }

$port = Join-Path $PSScriptRoot $(if ($Nx) { '..\port-nx' } else { '..\port' }) | Resolve-Path
$executable = Join-Path $port "out\build\win-amd64-$($Configuration.ToLowerInvariant())\fear.exe"
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
New-Item -ItemType Directory -Force (Join-Path $port 'logs') | Out-Null
$log = Join-Path $port "logs\autoplay-$Tag-$stamp.log"
$captures = Join-Path $PSScriptRoot "..\private\captures\autoplay-$Tag-$stamp"
New-Item -ItemType Directory -Force $captures | Out-Null
$arguments = @('--no-fullscreen', '--mnk_mode', '--resolution', '720p', '--headless',
    '--game_data_root', "$port\assets", '--user_data_root', "$port\user-smoke",
    '--cache_root', "$port\cache-smoke", '--log_file', $log) + @($Extra -split '\s+' | Where-Object { $_ })
$process = Start-Process -FilePath $executable -WorkingDirectory $port -ArgumentList $arguments -PassThru
$start = Get-Date
$nextKey = 5; $nextCapture = $CaptureEvery
try {
    while (!$process.HasExited) {
        $elapsed = ((Get-Date) - $start).TotalSeconds
        if ($elapsed -ge $Seconds) { break }
        $process.Refresh()
        if ($process.MainWindowHandle -ne [IntPtr]::Zero -and
            [AutoInput]::GetForegroundWindow() -ne $process.MainWindowHandle) {
            [AutoInput]::SetForegroundWindow($process.MainWindowHandle) | Out-Null
        }
        if ($elapsed -ge $nextKey -and ($KeysUntil -eq 0 -or $elapsed -lt $KeysUntil)) {
            [AutoInput]::Tap([uint16]$vk); $nextKey += $KeyEvery
        }
        if ($elapsed -ge $nextCapture) {
            & (Join-Path $PSScriptRoot 'capture_window.ps1') -ProcessId $process.Id -Out $captures | Out-Null
            $nextCapture += $CaptureEvery
        }
        Start-Sleep -Milliseconds 250
    }
} finally {
    if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
}
"log=$log captures=$captures"
