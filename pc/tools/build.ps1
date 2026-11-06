# Builds the PC game DLLs (GameClient.dll, GameServer.dll, ClientFx.fxd) as 32-bit from the
# public tools source in third_party/fear-publictools, with VS 2022's x86 toolset.
# Output: build/pc/game/{GameClient.dll,GameServer.dll,ClientFx.fxd}.
param(
    [switch]$Clean,
    [string]$Config = 'Release'
)
$ErrorActionPreference = 'Stop'
$repo = Resolve-Path (Join-Path $PSScriptRoot '..\..')
$sdk = Join-Path $repo 'third_party\fear-publictools'
$build = Join-Path $repo 'build\pc\game'
if (-not (Test-Path (Join-Path $sdk 'Game\Game.sln'))) {
    throw "No public tools source at $sdk. Run pc/tools/setup.ps1 first."
}
if ($Clean -and (Test-Path $build)) { Remove-Item -Recurse -Force $build }

$vswhere = "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe"
$vs = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vs) { throw 'Visual Studio with the C++ x86/x64 tools is required.' }
$cmake = Join-Path $repo '.tools\cmake\cmake-3.31.8-windows-x86_64\bin\cmake.exe'
$ninja = Join-Path $repo '.tools\ninja\ninja.exe'
if (-not (Test-Path $cmake)) { $cmake = 'cmake' }
if (-not (Test-Path $ninja)) { $ninja = 'ninja' }

# x86 developer environment. VsDevCmd.bat breaks on PATH entries containing parentheses, so
# this process drops them first; the developer environment adds back what the build needs.
$env:PATH = ($env:PATH -split ';' | Where-Object { $_ -and $_ -notmatch '[()]' }) -join ';'
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x86 -host_arch=x64 -no_logo' | Out-Null
& $cmake -S "$repo\pc\game" -B $build -G Ninja "-DCMAKE_MAKE_PROGRAM=$ninja" -DCMAKE_BUILD_TYPE=RelWithDebInfo `
    "-DFEAR_SDK_CONFIG=$Config" -DCMAKE_C_COMPILER=cl -DCMAKE_CXX_COMPILER=cl
if ($LASTEXITCODE -ne 0) { throw "Configure failed ($LASTEXITCODE)." }
& $cmake --build $build -- -k 0
if ($LASTEXITCODE -ne 0) { throw "Build failed ($LASTEXITCODE)." }
& (Join-Path $build 'core\fearpc_core_tests.exe')
if ($LASTEXITCODE -ne 0) { throw 'pc/core unit tests failed.' }
Get-ChildItem $build -Include GameClient.dll, GameServer.dll, ClientFx.fxd -Recurse | Select-Object Name, Length
