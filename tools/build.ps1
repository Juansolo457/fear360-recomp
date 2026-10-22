# Build port/ (the ReXGlue F.E.A.R. project) inside a VS 2022 x64 developer environment.
# Toolchain: .tools/ (rexglue-sdk, llvm, cmake, ninja), currently a junction to the N3
# project's pinned copy. Codegen runs as part of the build when the manifest changes.
#   pwsh -NoProfile -File tools/build.ps1 [-Configuration RelWithDebInfo] [-Jobs 12]
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [ValidateRange(1,64)][int]$Jobs = 12
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$port = Join-Path $root 'port'
$tools = Join-Path $root '.tools'
$cmake = "$tools/cmake/cmake-3.31.8-windows-x86_64/bin/cmake.exe"
if (!(Test-Path $cmake)) { throw "Toolchain missing at $tools." }
$xex = Join-Path $port 'assets/default.xex'
$expected = 'e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792'
if ((Get-FileHash $xex -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'port/assets/default.xex is not the supported F.E.A.R. executable.'
}

# vcvars64.bat breaks on quoted PATH entries; the DevShell module does not.
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -version '[17.0,18.0)' -property installationPath
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:PATH = "$tools/llvm/bin;$tools/ninja;$env:PATH"

$preset = 'win-amd64-' + $Configuration.ToLowerInvariant()
Push-Location $port
try {
    & $cmake --preset $preset "-DCMAKE_PREFIX_PATH=$tools/rexglue-sdk/win-amd64"
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    & $cmake --build --preset $preset --parallel $Jobs
    if ($LASTEXITCODE) { throw 'Build failed.' }
    Write-Host "Built port/out/build/$preset/fear.exe"
} finally { Pop-Location }
