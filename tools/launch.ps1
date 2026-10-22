# Launch the F.E.A.R. recomp (port/) for testing: fullscreen at the display's native size, the
# guest told it is on a 1280x720 display (as on a real 360; also avoids the 4:3 squeeze ReXGlue
# gives on high-DPI desktops), rendering at a multiple of 720p with 16x anisotropic filtering.
#
#   pwsh -NoProfile -File tools/launch.ps1                # 3x = 3840x2160 internal, fullscreen
#   pwsh -NoProfile -File tools/launch.ps1 -Scale 2 -Windowed
#   pwsh -NoProfile -File tools/launch.ps1 -Nx -Extra '--nx_renderer native'   # port-nx build
[CmdletBinding()]
param(
    [ValidateRange(1,3)][int]$Scale = 3,
    [ValidateSet(0,1,2,3,4,5)][int]$Anisotropic = 5,   # SDK enum: 5 = 16x
    [switch]$Windowed,
    [switch]$Nx,          # run port-nx (fear-nx's patched SDK + renderer) instead of port
    [string]$Extra = '',  # more flags, space separated
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$port = Join-Path $PSScriptRoot $(if ($Nx) { '..\port-nx' } else { '..\port' }) | Resolve-Path
$running = Get-Process -Name fear -ErrorAction SilentlyContinue
if ($running) { throw "fear.exe is already running (pid $($running.Id -join ', ')). Quit it first." }
$executable = Join-Path $port "out\build\win-amd64-$($Configuration.ToLowerInvariant())\fear.exe"
New-Item -ItemType Directory -Force (Join-Path $port 'logs') | Out-Null
$log = Join-Path $port ('logs\run-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$arguments = @(
    $(if ($Windowed) { '--no-fullscreen' } else { '--fullscreen' }),
    '--resolution', '720p', '--resolution_scale', "$Scale", '--anisotropic_override', "$Anisotropic",
    '--game_data_root', "$port\assets", '--user_data_root', "$port\user",
    '--cache_root', "$port\cache", '--log_file', $log) + @($Extra -split '\s+' | Where-Object { $_ })
if ($Nx) {
    # fear-nx's renderer has its own scale cvar (its images are Scale x the game's 1280x720).
    $arguments += @('--nx_native_resolution_scale', "$Scale")
}
$process = Start-Process -FilePath $executable -WorkingDirectory $port -ArgumentList $arguments -PassThru
"pid $($process.Id), log $log"
