# Recreate third_party/rexglue-sdk-nx, the patched ReXGlue SDK that tools/build-nx.ps1 builds:
# ReXGlue v0.10.0 (c94f5eb), then patches/rexglue-sdk-nx/ in order (0000 is fear-nx's
# native-vulkan SDK patch, 0001 on are this project's), checked against the exact source tree the
# release was built from, then the SDK's submodules. See docs/SETUP.md.
#   pwsh -NoProfile -File tools/setup-sdk.ps1                 # clone from GitHub
#   pwsh -NoProfile -File tools/setup-sdk.ps1 -Force          # replace an existing checkout
#   pwsh -NoProfile -File tools/setup-sdk.ps1 -Source <clone> # from a local ReXGlue clone
[CmdletBinding()]
param(
    [string]$Source = 'https://github.com/rexglue/rexglue-sdk.git',
    [switch]$Force,
    [switch]$NoSubmodules
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$sdk = Join-Path $root 'third_party\rexglue-sdk-nx'
$base = 'c94f5ebdcb3c9d1a460ca48e04f9758448f8d518'      # ReXGlue v0.10.0
$fearNxTree = 'c7fb3c0f1a588b268115bf0dceb24f0c26eb1ae0' # after patch 0000
$expectedTree = '99ce0fd7fbd7fe5b83af92b6f7140c984e4fd3a5' # after the last patch

function Invoke-Git {
    & git.exe @args
    if ($LASTEXITCODE) { throw "git $($args -join ' ') failed ($LASTEXITCODE)." }
}

if (!(Get-Command git -ErrorAction SilentlyContinue)) { throw 'Git is not installed or not on PATH.' }
if (Test-Path $sdk) {
    if (!$Force) { throw "$sdk exists already. Run with -Force to replace it." }
    Remove-Item -Recurse -Force $sdk
}
New-Item -ItemType Directory -Force (Split-Path $sdk) | Out-Null

Write-Host "Cloning ReXGlue from $Source"
# Some SDK shader files sit deep enough to pass Windows' 260-character path limit.
Invoke-Git -c core.longpaths=true clone --no-checkout $Source $sdk
Invoke-Git -C $sdk config core.longpaths true
Invoke-Git -C $sdk checkout --quiet --detach $base

$patches = Get-ChildItem (Join-Path $root 'patches\rexglue-sdk-nx') -Filter '*.patch' | Sort-Object Name
if (!$patches) { throw 'No patches found in patches/rexglue-sdk-nx.' }
# The commits only need some identity; the check below compares source trees, not commits.
$identity = @('-c', 'user.name=fear360-recomp setup', '-c', 'user.email=setup@localhost')
Write-Host "Applying $($patches.Count) patches"
Invoke-Git -C $sdk @identity am --quiet $patches[0].FullName
if ((& git -C $sdk rev-parse 'HEAD^{tree}') -ne $fearNxTree) {
    throw 'The SDK tree after the fear-nx patch is not the expected one.'
}
if ($patches.Count -gt 1) {
    Invoke-Git -C $sdk @identity am --quiet @($patches | Select-Object -Skip 1 | ForEach-Object FullName)
}
$tree = & git -C $sdk rev-parse 'HEAD^{tree}'
if ($tree -ne $expectedTree) {
    throw "The patched SDK tree is $tree, not ${expectedTree}: patches/rexglue-sdk-nx has " +
          'changed without this script, or a patch applied differently.'
}
if (& git -C $sdk status --porcelain) {
    throw 'The SDK checkout on disk differs from its commit (a file failed to write?): see git status.'
}
Invoke-Git -C $sdk switch --quiet -c fear360

if (!$NoSubmodules) {
    Write-Host 'Fetching the SDK submodules (SDL, glslang, FFmpeg, ...)'
    Invoke-Git -C $sdk submodule update --init --recursive
}
Write-Host "Patched SDK ready at $sdk (tree $tree)."
