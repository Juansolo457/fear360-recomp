# Starts PC F.E.A.R. with our game DLLs without changing the game folder.
#
# The engine reads its archives from an archive list (-archcfg); later entries override earlier
# ones, and a folder entry serves loose files. This writes build/pc/fearpc.archcfg listing the
# game's own archives by absolute path, then build/pc/overlay, which holds the DLLs built by
# pc/tools/build.ps1. Saves and settings go to user_dir (-userdirectory), not the user profile.
#
# Paths come from private/pc.toml (game_dir, user_dir).
param(
    [switch]$Retail,          # run the game's own DLLs (same archive list, no overlay)
    [string[]]$Extra = @(),   # more engine arguments, e.g. '+windowed','1'
    [switch]$Wait
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$cfgFile = Join-Path $repo 'private\pc.toml'
if (-not (Test-Path $cfgFile)) { throw "Missing $cfgFile (game_dir, user_dir)." }
$cfg = @{}
foreach ($line in Get-Content $cfgFile) {
    if ($line -match "^\s*(\w+)\s*=\s*'([^']*)'") { $cfg[$Matches[1]] = $Matches[2] }
}
$game = $cfg.game_dir; $user = $cfg.user_dir
if (-not (Test-Path (Join-Path $game 'FEAR.exe'))) { throw "No FEAR.exe in game_dir $game." }
New-Item -ItemType Directory -Force $user | Out-Null

$out = Join-Path $repo 'build\pc'
$overlay = Join-Path $out 'overlay'
New-Item -ItemType Directory -Force $overlay | Out-Null
Get-ChildItem $overlay -File | Remove-Item
$lines = Get-Content (Join-Path $game 'Default.archcfg') | Where-Object { $_.Trim() -and -not $_.Trim().StartsWith(';') } |
    ForEach-Object { Join-Path $game $_.Trim() }
if (-not $Retail) {
    foreach ($n in 'GameClient.dll', 'GameServer.dll', 'ClientFx.fxd') {
        $built = Join-Path $out "game\$n"
        if (-not (Test-Path $built)) { throw "Missing $built. Run pc/tools/build.ps1." }
        Copy-Item $built $overlay
    }
    $lines += $overlay
}
$archcfg = Join-Path $out 'fearpc.archcfg'
Set-Content -Path $archcfg -Value $lines -Encoding ascii

$args = @('-archcfg', "`"$archcfg`"", '-userdirectory', "`"$user`"") + $Extra
Write-Host "FEAR.exe $($args -join ' ')"
$p = Start-Process -FilePath (Join-Path $game 'FEAR.exe') -WorkingDirectory $game -ArgumentList $args -PassThru
if ($Wait) { $p.WaitForExit(); exit $p.ExitCode }
$p.Id
