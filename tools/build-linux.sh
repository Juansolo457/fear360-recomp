#!/usr/bin/env bash
# Build F.E.A.R. or F.E.A.R. Files on Linux x86-64 (the counterpart of tools/build-nx.ps1): the
# patched SDK tools/setup-sdk.sh made, built from source with the Vulkan backend and the Xenos
# plugin linked statically, then the game translation with that SDK's own rexglue tool, then the
# game with the renderer's Vulkan backend (docs/VULKAN_RENDERER.md). The first build compiles the
# whole SDK. Needs clang 20 or later, CMake 3.25+, Ninja and the SDK's system libraries
# (docs/SETUP.md, "Linux").
#   tools/build-linux.sh                          # F.E.A.R. (port-nx)
#   tools/build-linux.sh --project port-files     # F.E.A.R. Files, same host code
#   tools/build-linux.sh --config Release --jobs 12
# FEAR_SDK_DIR: the SDK checkout (default third_party/rexglue-sdk-nx); FEAR_BUILD_DIR: the build
# folder (default <project>/out/build/<preset>). tools/build-deck.sh sets both.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
project=port-nx
config=RelWithDebInfo
jobs=$(( $(nproc) > 2 ? $(nproc) - 2 : 1 ))
while [[ $# -gt 0 ]]; do
  case "$1" in
    --project) project="$2"; shift ;;
    --config) config="$2"; shift ;;
    --jobs) jobs="$2"; shift ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
# Each project: its executable's SHA-256 (assets/default.xex), title and program name.
case "$project" in
  port-nx) sha=e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792
           title=F.E.A.R. name=fear ;;
  port-files) sha=a51deed1dc0842af32a3a2b811674552f4ae5b6339ce8b9abe0eabe14ba6c7eb
              title="F.E.A.R. Files" name=fearfiles ;;
  *) echo "unknown project $project (port-nx, port-files)" >&2; exit 2 ;;
esac
case "$config" in
  Debug|Release|RelWithDebInfo) ;;
  *) echo "unknown configuration $config" >&2; exit 2 ;;
esac
for cmd in cmake ninja "${CC:-clang}" "${CXX:-clang++}"; do
  command -v "$cmd" >/dev/null || { echo "$cmd is not installed (docs/SETUP.md, Linux)" >&2; exit 1; }
done

port="$root/$project"
sdk="${FEAR_SDK_DIR:-$root/third_party/rexglue-sdk-nx}"
[[ -f "$sdk/src/graphics/plugin_main.cpp" ]] || {
  echo "Patched SDK missing at $sdk: run tools/setup-sdk.sh" >&2; exit 1; }
xex="$port/assets/default.xex"
[[ -f "$xex" ]] || { echo "$project/assets/default.xex is missing (docs/SETUP.md)" >&2; exit 1; }
if [[ "$(sha256sum "$xex" | cut -d' ' -f1)" != "$sha" ]]; then
  echo "$project/assets/default.xex is not the supported $title executable" >&2
  exit 1
fi

preset="linux-amd64-${config,,}"
build="${FEAR_BUILD_DIR:-$port/out/build/$preset}"
# x86-64-v3 as on Windows (tools/build-nx.ps1 has why); the Steam Deck's Zen 2 has it. The presets
# name clang-20; any clang 20 or later works. Debug info in every configuration, Release too: a
# crash's backtrace (coredumpctl) and perf need the function names.
args=(--preset "$preset" -B "$build" -DREXSDK_DIR="$sdk"
      -DCMAKE_C_COMPILER="${CC:-clang}" -DCMAKE_CXX_COMPILER="${CXX:-clang++}"
      "-DCMAKE_C_FLAGS=-march=x86-64-v3 -g" "-DCMAKE_CXX_FLAGS=-march=x86-64-v3 -g"
      -DREXGLUE_USE_VULKAN=ON -DREXGLUE_GPU_XENOS_STATIC=ON -DREXGLUE_BUILD_TESTS=OFF)
cd "$port"
cmake "${args[@]}"
# Generate (again) when the executable or fear_manifest.toml changed (a no-op otherwise), then
# reconfigure once if the generated sources didn't exist yet (the configure above created no
# fear_recomp target without them), then build.
first_time=0
[[ -f generated/default/sources.cmake ]] || first_time=1
cmake --build "$build" --parallel "$jobs" --target fear_codegen
if [[ $first_time -eq 1 ]]; then cmake "${args[@]}"; fi
cmake --build "$build" --parallel "$jobs"
echo "Built $build/$name"
