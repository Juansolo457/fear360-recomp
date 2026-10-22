# Headless boot check: start our own fear.exe with debug logging, stop it after N seconds, and
# print the end of the log. Surviving the run does not prove the game renders correctly.
# -Capture saves a screenshot of the window just before stopping it; -Extra passes more flags.
#   pwsh -NoProfile -File tools/smoke.ps1 [-Seconds 40] [-Capture] [-Extra '--no-clear_memory_page_state']
[CmdletBinding()]
param(
    [ValidateRange(5,300)][int]$Seconds = 40,
    [switch]$Capture,
    [string[]]$Extra = @(),
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo'
)
$ErrorActionPreference = 'Stop'
$port = Join-Path $PSScriptRoot '..\port' | Resolve-Path
$executable = Join-Path $port "out\build\win-amd64-$($Configuration.ToLowerInvariant())\fear.exe"
New-Item -ItemType Directory -Force (Join-Path $port 'logs') | Out-Null
$log = Join-Path $port ('logs\smoke-' + (Get-Date -Format 'yyyyMMdd-HHmmss') + '.log')
$info = [Diagnostics.ProcessStartInfo]::new($executable)
$info.WorkingDirectory = $port
$info.UseShellExecute = $false
$info.CreateNoWindow = $true
foreach ($argument in @('--headless', '--no-fullscreen', '--resolution', '720p',
    '--game_data_root', "$port\assets", '--user_data_root', "$port\user-smoke",
    '--cache_root', "$port\cache-smoke", '--log_file', $log, '--log_level', 'debug', '--log_flush_interval', '1') + $Extra) {
    $info.ArgumentList.Add($argument)
}
$process = [Diagnostics.Process]::Start($info)
$stopped = !$process.WaitForExit($Seconds * 1000)
if ($stopped -and $Capture) {
    & (Join-Path $PSScriptRoot 'capture_window.ps1') -ProcessId $process.Id -Out (Join-Path $PSScriptRoot '..\private\captures')
}
if ($stopped) { $process.Kill(); $process.WaitForExit() }
"stoppedByHarness=$stopped exitCode=$($process.ExitCode) log=$log"
if (Test-Path $log) { Get-Content $log -Tail 40 }
