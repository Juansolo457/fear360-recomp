# Build port-nx/: F.E.A.R. on fear-nx's patched ReXGlue SDK, built from source with the
# Vulkan backend and the Xenos plugin linked statically, plus fear-nx's native renderer.
# The first build compiles the whole SDK (FFmpeg subset, glslang, SDL3, Vulkan loader, ...),
# then generates the game translation with that SDK's own rexglue tool, reconfigures, and
# builds the game. Toolchain: .tools/ (LLVM 20, CMake, Ninja) inside a VS 2022 DevShell.
#   pwsh -NoProfile -File tools/build-nx.ps1 [-Configuration RelWithDebInfo] [-Jobs 14]
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release','RelWithDebInfo')][string]$Configuration = 'RelWithDebInfo',
    [ValidateRange(1,64)][int]$Jobs = 14
)
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$port = Join-Path $root 'port-nx'
$sdk = Join-Path $root 'third_party\rexglue-sdk-nx'
$tools = Join-Path $root '.tools'
$cmake = "$tools/cmake/cmake-3.31.8-windows-x86_64/bin/cmake.exe"
if (!(Test-Path $cmake)) { throw "Toolchain missing at $tools." }
if (!(Test-Path "$sdk/src/graphics/plugin_main.cpp")) { throw "Patched SDK missing at $sdk." }
$xex = Join-Path $port 'assets/default.xex'
$expected = 'e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792'
if ((Get-FileHash $xex -Algorithm SHA256).Hash.ToLowerInvariant() -ne $expected) {
    throw 'port-nx/assets/default.xex is not the supported F.E.A.R. executable.'
}

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -version '[17.0,18.0)' -property installationPath
Import-Module (Join-Path $vs 'Common7\Tools\Microsoft.VisualStudio.DevShell.dll')
Enter-VsDevShell -VsInstallPath $vs -SkipAutomaticLocation -DevCmdArguments '-arch=x64 -host_arch=x64' | Out-Null
$env:PATH = "$tools/llvm/bin;$tools/ninja;$env:PATH"

$preset = 'win-amd64-' + $Configuration.ToLowerInvariant()
$sdkPath = ($sdk -replace '\\', '/')
# fear-nx's clock patch divides an unsigned __int128 (__udivti3), which MSVC's runtime lacks;
# compiler-rt's builtins ship with the LLVM in .tools. The MSVC-target clang driver ignores
# --rtlib=compiler-rt, so the library is passed by path.
$builtins = Get-ChildItem "$tools/llvm/lib/clang/*/lib/windows/clang_rt.builtins-x86_64.lib" | Select-Object -First 1
if (!$builtins) { throw 'clang_rt.builtins-x86_64.lib not found in the LLVM toolchain.' }
$builtinsPath = ($builtins.FullName -replace '\\', '/')
# The SDK's own presets build for x86-64-v2 (its byte-swap code uses SSSE3 intrinsics).
$configureArgs = @('--preset', $preset,
    "-DREXSDK_DIR=$sdkPath",
    '-DCMAKE_C_FLAGS=-march=x86-64-v2', '-DCMAKE_CXX_FLAGS=-march=x86-64-v2',
    "-DCMAKE_EXE_LINKER_FLAGS=$builtinsPath", "-DCMAKE_SHARED_LINKER_FLAGS=$builtinsPath",
    '-DREXGLUE_USE_D3D12=ON', '-DREXGLUE_USE_VULKAN=ON', '-DREXGLUE_GPU_XENOS_STATIC=ON',
    '-DREXGLUE_BUILD_TESTS=OFF')
Push-Location $port
try {
    & $cmake @configureArgs
    if ($LASTEXITCODE) { throw 'CMake configure failed.' }
    if (!(Test-Path (Join-Path $port 'generated/default/sources.cmake'))) {
        # First time: the generated sources do not exist yet, so the configure above
        # created no fear_recomp target. Generate, then configure again.
        & $cmake --build --preset $preset --parallel $Jobs --target fear_codegen
        if ($LASTEXITCODE) { throw 'Code generation failed.' }
        & $cmake @configureArgs
        if ($LASTEXITCODE) { throw 'CMake re-configure failed.' }
    }
    & $cmake --build --preset $preset --parallel $Jobs
    if ($LASTEXITCODE) { throw 'Build failed.' }
    Write-Host "Built port-nx/out/build/$preset/fear.exe"
} finally { Pop-Location }
