# Launch the F.E.A.R. recomp (port/) for testing: fullscreen at the display's native size, the
# guest told it is on a 1280x720 display (as on a real 360; also avoids the 4:3 squeeze ReXGlue
# gives on high-DPI desktops), rendering at a multiple of 720p with 16x anisotropic filtering.
#
#   pwsh -NoProfile -File tools/launch.ps1                # 3x = 3840x2160 internal, fullscreen, auto fps
#   ... -Nx -FrameRate 30 -Timer                                               # the console's pacing
#   pwsh -NoProfile -File tools/launch.ps1 -Scale 2 -Windowed -FrameRate 60
#   pwsh -NoProfile -File tools/launch.ps1 -Nx -Extra '--nx_renderer native'   # port-nx build
#   ... -Nx -TexturePack private/texture_pack -Extra '--nx_renderer native'    # with PC textures
#   ... -Files -Extra '--nx_renderer native'      # F.E.A.R. Files (port-files, port-nx's host code)
#   ... -Collection -Extra '--nx_renderer native' # the collection menu: F.E.A.R. and F.E.A.R. Files
#
# -Collection writes private/collection.toml with both games (each its own data, user and cache
# folders, log, and texture pack when one is built: private/texture_pack, private/files/
# texture_pack) and starts F.E.A.R. (or with -Files, F.E.A.R. Files) at the collection menu
# (port-nx/src/fear_collection.h); picking the other game starts its program.
#
# port-nx and port-files keep their settings in <user data>/settings.toml, written by PC Settings
# in the game's main menu (port-nx/src/fear_settings.h). For them this script passes a setting
# (-Scale, -Windowed, -FrameRate, -TexturePack, -LookRamp, -NoSoftLock) only when it is given:
# a flag wins over the saved value for that run.
#
#   ... -Collection -Invulnerable   # the smoke test (#55): both games, the player can't die
[CmdletBinding()]
param(
    [ValidateRange(1,3)][int]$Scale = 3,
    [ValidateSet(0,1,2,3,4,5)][int]$Anisotropic = 5,   # SDK enum: 5 = 16x
    [switch]$Windowed,
    [switch]$Nx,          # run port-nx (fear-nx's patched SDK + renderer) instead of port
    [switch]$Files,       # run F.E.A.R. Files (port-files: the same host code as port-nx)
    [switch]$Collection,  # the collection menu over F.E.A.R. and F.E.A.R. Files (implies -Nx)
    # The game presents every 2 vblanks: 30 fps at the console's 60 Hz.
    # port-nx follows the display's own vblank (port-nx/src/display_vblank.h): each frame is
    # shown for a whole number of refreshes, the fewest that keep it at or under FrameRate
    # (auto = the game's default, 120): 120 fps on 240 or 120 Hz, 72 on 144 Hz, 82.5 on 165 Hz,
    # 60 on 60 Hz; 30 gives the console's rate on any of those. (The tracers that broke above
    # ~90 were the renderer's stale vertex data, fixed by fear_vertex_exact.)
    [ValidateScript({ $_ -eq 'auto' -or ([double]::TryParse($_, [ref]$null) -and [double]$_ -ge 20 -and [double]$_ -le 240) })]
    [string]$FrameRate = 'auto',
    # A software timer for the vblank instead (port, or port-nx with -Timer), at FrameRate x 2
    # (up to 480 Hz with SDK patch 0003); auto picks the highest rate up to 120 that divides the
    # display's refresh. It drifts against the display, so a frame now and then is shown a
    # refresh early or late.
    [switch]$Timer,
    # Swapchain present mode. auto: mailbox with port-nx's display pacing (frames go out right
    # after a vblank, so immediate would tear them), else the SDK's pick, immediate when the
    # driver offers it. mailbox = no tearing, low latency; fifo = true vsync, more latency.
    [ValidateSet('auto', 'immediate', 'mailbox', 'fifo')][string]$Present = 'auto',
    # A texture pack folder built by tools/texture_pack.py (port-nx only); see docs/TEXTURES.md.
    [string]$TexturePack = '',
    # Right stick at its edge (port-nx): console = as shipped (0.3 s wait, then a 0.4 s ramp up
    # to 360 deg/s, restarting every time the stick stops), nodelay, instant, or off (no boost,
    # turn rate follows the stick). See port-nx/src/look_tuning.h.
    [ValidateSet('console', 'nodelay', 'instant', 'off')][string]$LookRamp = 'off',
    [switch]$NoSoftLock,  # no aim-assist slowdown of turning near enemies
    # For the smoke test (#55): the player can't die, in single player and Instant Action. Where
    # a hit would have killed, health refills to full and play carries on, with no death effect
    # (fear_invulnerable: the PC's test console variable HealthTest, still in both 360 games;
    # port-nx/src/look_tuning.h).
    [switch]$Invulnerable,
    [string]$Extra = '',  # more flags, space separated
    # Another F.E.A.R. program instead of the build's (port-nx; with -Collection, F.E.A.R.'s
    # entry): the A side of an A/B, e.g. private/ab/A-master-8db2298/fear.exe, with its own
    # rexruntimerd.dll beside it. The log is named after its folder. Compare the two logs with
    # tools/analysis/abcompare.py.
    [string]$Exe = '',
    # Each game's cache folder (shaders and the driver's pipelines), in its port folder. A name
    # not used before starts empty, as on a new PC: e.g. -Cache cache-new1 to watch the shader
    # warm-up (#53, port-nx/src/shader_warmup.h).
    [ValidatePattern('^[\w.-]+$')][string]$Cache = 'cache',
    # Each game's user folder (saves, profile, settings.toml), in its port folder, the same way: a
    # new name is a new player (first-run settings, no saves), e.g. -User user-run1 -Cache
    # cache-run1 for a smoke-test run from scratch (docs/SMOKE-1.0.md).
    [ValidatePattern('^[\w.-]+$')][string]$User = 'user',
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
if ($Files -or $Collection) { $Nx = $true }
$given = $PSBoundParameters
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'

# One game's program, folder and arguments: 'port' (the stock-SDK build), 'nx' (F.E.A.R.) or
# 'files' (F.E.A.R. Files).
function Get-Run([string]$Title, [string]$Pack) {
$isNx = $Title -ne 'port'
$port = Join-Path $PSScriptRoot $(switch ($Title) { 'files' { '..\port-files' } 'nx' { '..\port-nx' } default { '..\port' } }) | Resolve-Path
$name = if ($Title -eq 'files') { 'fearfiles' } else { 'fear' }
$executable = Join-Path $port "out\build\win-amd64-$($Configuration.ToLowerInvariant())\$name.exe"
$tag = ''
if ($Exe -and $Title -eq 'nx') {
    $executable = (Resolve-Path $Exe).Path
    $tag = '-' + (Split-Path (Split-Path $executable -Parent) -Leaf)
}
New-Item -ItemType Directory -Force (Join-Path $port 'logs') | Out-Null
$log = Join-Path $port "logs\run-$stamp$tag.log"
$arguments = @(
    '--resolution', '720p', '--resolution_scale', "$Scale", '--anisotropic_override', "$Anisotropic",
    '--game_data_root', "$port\assets", '--user_data_root', "$port\$User",
    '--cache_root', "$port\$Cache", '--log_file', $log) + @($Extra -split '\s+' | Where-Object { $_ })
if (!$isNx -or $given.ContainsKey('Windowed')) {
    $arguments += $(if ($Windowed) { '--no-fullscreen' } else { '--fullscreen' })
}
if ($isNx) {
    # fear-nx's renderer has its own scale cvar (its images are Scale x the game's 1280x720).
    if ($given.ContainsKey('Scale')) { $arguments += @('--nx_native_resolution_scale', "$Scale") }
    if ($given.ContainsKey('LookRamp')) { $arguments += @('--fear_look_ramp', $LookRamp) }
    if ($NoSoftLock) { $arguments += '--no-fear_aim_softlock' }
    if ($Invulnerable) { $arguments += '--fear_invulnerable' }
}
if ($Pack) {
    if (!$isNx) { throw '-TexturePack needs -Nx (the texture pack hooks are in the port-nx renderer).' }
    $resolved = Resolve-Path $Pack
    if (!(Test-Path (Join-Path $resolved 'index.tsv'))) { throw "No index.tsv in $resolved." }
    $arguments += @('--fear_texture_pack', ($resolved.Path -replace '\\', '/'))
}
if ($isNx -and !$Timer) {
    # The game reports the rate it picked in its log ("vblank from the display").
    if ($FrameRate -ne 'auto') { $arguments += @('--fear_frame_rate_limit', ([double]$FrameRate).ToString($invariant)) }
} else {
    if ($isNx) { $arguments += @('--fear_vblank_source', 'timer') }
    if ($FrameRate -eq 'auto') {
        $refresh = (Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
            Where-Object { $_.CurrentRefreshRate } | Measure-Object CurrentRefreshRate -Maximum).Maximum
        if (!$refresh) { $refresh = 60 }
        $divisor = [math]::Ceiling($refresh / 120)
        $fps = $refresh / $divisor
        Write-Host "display ${refresh} Hz: $fps fps on a timer (each frame shown for $divisor refreshes, now and then one more or less)"
    } else {
        $fps = [double]$FrameRate
    }
    $arguments += @('--video_mode_refresh_rate', ($fps * 2).ToString($invariant))
}
# The presenter takes the first allowed mode of immediate > mailbox > fifo_relaxed > fifo.
switch ($Present) {
    'mailbox' { $arguments += @('--no-vulkan_allow_present_mode_immediate') }
    'fifo' {
        $arguments += @('--no-vulkan_allow_present_mode_immediate', '--no-vulkan_allow_present_mode_mailbox',
            '--no-vulkan_allow_present_mode_fifo_relaxed')
    }
}
@{ Name = $name; Executable = $executable; Port = $port; Arguments = $arguments; Log = $log }
}

foreach ($name in 'fear', 'fearfiles') {
    if (!$Collection -and $name -ne $(if ($Files) { 'fearfiles' } else { 'fear' })) { continue }
    $running = Get-Process -Name $name -ErrorAction SilentlyContinue
    if ($running) { throw "$name.exe is already running (pid $($running.Id -join ', ')). Quit it first." }
}

if ($Collection) {
    # Each game's texture pack, when it has been built (docs/TEXTURES.md).
    $root = Join-Path $PSScriptRoot '..' | Resolve-Path
    $packs = @{ nx = (Join-Path $root 'private\texture_pack'); files = (Join-Path $root 'private\files\texture_pack') }
    $titles = @(
        @{ Id = 'fear'; Title = 'nx'; Name = 'F.E.A.R.'; Subtitle = 'First Encounter Assault Recon' },
        @{ Id = 'files'; Title = 'files'; Name = 'F.E.A.R. Files'; Subtitle = 'Extraction Point and Perseus Mandate' })
    $toml = @('# The F.E.A.R. collection menu (port-nx/src/fear_collection.h), written by tools/launch.ps1 -Collection.')
    $runs = @{}
    foreach ($t in $titles) {
        $pack = if (Test-Path (Join-Path $packs[$t.Title] 'index.tsv')) { $packs[$t.Title] } else { '' }
        $run = Get-Run $t.Title $pack
        $runs[$t.Id] = $run
        $toml += '', '[[title]]', "id = '$($t.Id)'", "name = '$($t.Name)'", "subtitle = '$($t.Subtitle)'",
            "exe = '$($run.Executable)'", "working_dir = '$($run.Port)'",
            ('args = [' + (($run.Arguments | ForEach-Object { "'$_'" }) -join ', ') + ']')
    }
    $file = Join-Path $root 'private\collection.toml'
    New-Item -ItemType Directory -Force (Split-Path $file) | Out-Null
    Set-Content -Path $file -Value $toml -Encoding utf8NoBOM
    $first = $runs[$(if ($Files) { 'files' } else { 'fear' })]
    $arguments = $first.Arguments + @('--fear_collection', $file)
    $process = Start-Process -FilePath $first.Executable -WorkingDirectory $first.Port -ArgumentList $arguments -PassThru
    "collection $file; pid $($process.Id), log $($first.Log)"
    return
}

$run = Get-Run $(if ($Files) { 'files' } elseif ($Nx) { 'nx' } else { 'port' }) $TexturePack
$process = Start-Process -FilePath $run.Executable -WorkingDirectory $run.Port -ArgumentList $run.Arguments -PassThru
"pid $($process.Id), log $($run.Log)"
