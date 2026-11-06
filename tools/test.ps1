# Build and run the unit tests (port-nx/tests/): host code that runs without the game. Needs the
# toolchain in .tools/ and the SDK checkout (for its copy of Catch2), not the game's files.
#   pwsh -NoProfile -File tools/test.ps1
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$tools = Join-Path $root '.tools'
$cmake = "$tools/cmake/cmake-3.31.8-windows-x86_64/bin/cmake.exe"
if (!(Test-Path $cmake)) { throw "Toolchain missing at $tools." }
$catch2 = Join-Path $root 'third_party\rexglue-sdk-nx\thirdparty\catch2'
if (!(Test-Path "$catch2/CMakeLists.txt")) { throw "Catch2 missing at $catch2 (tools/setup-sdk.ps1)." }

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -version '[17.0,18.0)' -property installationPath
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:PATH = "$tools/llvm/bin;$tools/ninja;$env:PATH"

$source = Join-Path $root 'port-nx/tests'
$build = Join-Path $root 'port-nx/out/tests'
& $cmake -S $source -B $build -G Ninja -DCMAKE_BUILD_TYPE=Release `
    -DCMAKE_CXX_COMPILER=clang++ `
    "-DFEAR_CATCH2_DIR=$($catch2 -replace '\\', '/')"
if ($LASTEXITCODE) { throw 'CMake configure failed.' }
& $cmake --build $build
if ($LASTEXITCODE) { throw 'Build failed.' }
& (Join-Path $build 'fear_tests.exe')
if ($LASTEXITCODE) { throw 'Tests failed.' }
