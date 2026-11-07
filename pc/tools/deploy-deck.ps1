# Puts the PC build on a Steam Deck, for the Steam F.E.A.R. running under Proton.
#
# The PC engine (FEAR.exe) is a closed 32-bit Windows program, so there is no native Linux
# build: on the Deck the game runs through Proton and loads our DLLs as it does on Windows.
# Nothing in the game's folder changes. This copies GameClient.dll, GameServer.dll and
# ClientFx.fxd to ~/fear-pc/overlay on the Deck and writes ~/fear-pc/fearpc.archcfg: the Deck
# install's own archives (from its Default.archcfg), then the overlay, as Proton's Z: paths.
# Saves and settings go to ~/fear-pc/user. Then set the game's launch options in Steam to the
# line printed at the end; clear them to play the game as it shipped.
#   pwsh -NoProfile -File pc/tools/deploy-deck.ps1 [-SshHost deck-ts]
param(
    [string]$SshHost = 'deck-ts',
    [string]$GameDir = '/home/deck/.local/share/Steam/steamapps/common/FEAR Ultimate Shooter Edition',
    [string]$Dest = '/home/deck/fear-pc'
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$built = Join-Path $repo 'build\pc\game'
$dlls = 'GameClient.dll', 'GameServer.dll', 'ClientFx.fxd' | ForEach-Object { Join-Path $built $_ }
foreach ($d in $dlls) { if (-not (Test-Path $d)) { throw "Missing $d. Run pc/tools/build.ps1." } }

function Invoke-Deck([string]$command) {
    $out = & ssh -o BatchMode=yes $SshHost $command
    if ($LASTEXITCODE) { throw "ssh ${SshHost}: '$command' failed ($LASTEXITCODE)." }
    return $out
}
# A Linux path as Proton's Windows programs see it.
function WinePath([string]$path) { 'Z:' + ($path -replace '/', '\') }

Invoke-Deck "test -f '$GameDir/FEAR.exe' && test -f '$GameDir/Default.archcfg'" | Out-Null
Invoke-Deck "mkdir -p '$Dest/overlay' '$Dest/user'" | Out-Null
& scp -q -o BatchMode=yes @dlls "${SshHost}:$Dest/overlay/"
if ($LASTEXITCODE) { throw 'Copying the DLLs failed.' }

$archives = Invoke-Deck "cat '$GameDir/Default.archcfg'" |
    ForEach-Object { $_.Trim() } | Where-Object { $_ -and -not $_.StartsWith(';') } |
    ForEach-Object { WinePath "$GameDir/$_" }
$lines = @($archives) + (WinePath "$Dest/overlay")
$local = Join-Path $repo 'build\pc\fearpc-deck.archcfg'
[IO.File]::WriteAllText($local, ($lines -join "`r`n") + "`r`n", [Text.Encoding]::ASCII)
& scp -q -o BatchMode=yes $local "${SshHost}:$Dest/fearpc.archcfg"
if ($LASTEXITCODE) { throw 'Copying the archive list failed.' }

Invoke-Deck "ls -l '$Dest/overlay' '$Dest/fearpc.archcfg'"
Write-Host ''
Write-Host 'Steam > F.E.A.R. > Properties > Launch Options:'
Write-Host "%command% -archcfg `"$(WinePath "$Dest/fearpc.archcfg")`" -userdirectory `"$(WinePath "$Dest/user")`""
