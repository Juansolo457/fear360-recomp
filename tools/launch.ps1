# Launch the F.E.A.R. recomp (port/) for testing: fullscreen at the display's native size, the
# guest told it is on a 1280x720 display (as on a real 360; also avoids the 4:3 squeeze ReXGlue
# gives on high-DPI desktops), rendering at a multiple of 720p with 16x anisotropic filtering.
#
#   pwsh -NoProfile -File tools/launch.ps1                # 3x = 3840x2160 internal, fullscreen, auto fps
#   ... -Nx -FrameRate 30 -Timer                                               # the console's pacing
#   pwsh -NoProfile -File tools/launch.ps1 -Scale 2 -Windowed -FrameRate 60
#   pwsh -NoProfile -File tools/launch.ps1 -Nx -Extra '--nx_renderer native'   # port-nx build
#   ... -Nx -TexturePack private/texture_pack -Extra '--nx_renderer native'    # with PC textures
[CmdletBinding()]
param(
    [ValidateRange(1,3)][int]$Scale = 3,
    [ValidateSet(0,1,2,3,4,5)][int]$Anisotropic = 5,   # SDK enum: 5 = 16x
    [switch]$Windowed,
    [switch]$Nx,          # run port-nx (fear-nx's patched SDK + renderer) instead of port
    # The game presents every 2 vblanks: 30 fps at the console's 60 Hz. Above ~90 fps the 360's
    # effect code breaks (bullet tracers twitch and freeze mid-air).
    # port-nx follows the display's own vblank (port-nx/src/display_vblank.h): each frame is
    # shown for a whole number of refreshes, the fewest that keep it at or under FrameRate
    # (auto = 90): 80 fps on 240 Hz ("80 feels great… better than 90"), 72 on 144 Hz, 82.5 on
    # 165 Hz, 60 on 60 or 120 Hz; 30 gives the console's rate on any of those.
    [ValidateScript({ $_ -eq 'auto' -or ([double]::TryParse($_, [ref]$null) -and [double]$_ -ge 20 -and [double]$_ -le 240) })]
    [string]$FrameRate = 'auto',
    # A software timer for the vblank instead (port, or port-nx with -Timer), at FrameRate x 2
    # (up to 480 Hz with SDK patch 0003); auto picks the highest rate up to 90 that divides the
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
    $arguments += @('--nx_native_resolution_scale', "$Scale", '--fear_look_ramp', $LookRamp)
    if ($NoSoftLock) { $arguments += '--no-fear_aim_softlock' }
}
if ($TexturePack) {
    if (!$Nx) { throw '-TexturePack needs -Nx (the texture pack hooks are in the port-nx renderer).' }
    $pack = Resolve-Path $TexturePack
    if (!(Test-Path (Join-Path $pack 'index.tsv'))) { throw "No index.tsv in $pack." }
    $arguments += @('--fear_texture_pack', ($pack.Path -replace '\\', '/'))
}
$invariant = [System.Globalization.CultureInfo]::InvariantCulture
if ($Nx -and !$Timer) {
    # The game reports the rate it picked in its log ("vblank from the display").
    if ($FrameRate -ne 'auto') { $arguments += @('--fear_frame_rate_limit', ([double]$FrameRate).ToString($invariant)) }
} else {
    if ($Nx) { $arguments += @('--fear_vblank_source', 'timer') }
    if ($FrameRate -eq 'auto') {
        $refresh = (Get-CimInstance Win32_VideoController -ErrorAction SilentlyContinue |
            Where-Object { $_.CurrentRefreshRate } | Measure-Object CurrentRefreshRate -Maximum).Maximum
        if (!$refresh) { $refresh = 60 }
        $divisor = [math]::Ceiling($refresh / 90)
        $fps = $refresh / $divisor
        "display ${refresh} Hz: $fps fps on a timer (each frame shown for $divisor refreshes, now and then one more or less)"
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
$process = Start-Process -FilePath $executable -WorkingDirectory $port -ArgumentList $arguments -PassThru
"pid $($process.Id), log $log"
