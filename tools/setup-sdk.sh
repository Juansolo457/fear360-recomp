#!/usr/bin/env bash
# Recreate the patched ReXGlue SDK that tools/build-linux.sh builds, on Linux (the counterpart of
# tools/setup-sdk.ps1, which has the details): ReXGlue v0.10.0 (c94f5eb), then
# patches/rexglue-sdk-nx/ in order, checked against the same source trees, then the SDK's
# submodules. The game's build compiles the SDK from this tree (docs/SETUP.md, "Linux").
#   tools/setup-sdk.sh                  # third_party/rexglue-sdk-nx
#   tools/setup-sdk.sh --force          # replace an existing checkout
#   tools/setup-sdk.sh --source <clone> # from a local ReXGlue clone
# FEAR_SDK_DIR puts the checkout elsewhere (tools/build-deck.sh keeps its own:
# third_party/rexglue-sdk-nx-deck).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
sdk="${FEAR_SDK_DIR:-$root/third_party/rexglue-sdk-nx}"
source=https://github.com/rexglue/rexglue-sdk.git
base=c94f5ebdcb3c9d1a460ca48e04f9758448f8d518        # ReXGlue v0.10.0
fear_nx_tree=c7fb3c0f1a588b268115bf0dceb24f0c26eb1ae0 # after patch 0000
expected_tree=8117089eb581fc9301aff06465cdc40343f1054e # after the last patch (as setup-sdk.ps1)
force=0
submodules=1
while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) force=1 ;;
    --source) source="$2"; shift ;;
    --no-submodules) submodules=0 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
command -v git >/dev/null || { echo "git is not installed" >&2; exit 1; }

if [[ -d "$sdk" ]]; then
  # A finished checkout is at the expected tree with no changes of its own; anything else stopped
  # partway or was changed by hand, and replacing it is --force's job.
  head=$(git -C "$sdk" rev-parse --verify --quiet 'HEAD^{tree}' 2>/dev/null || true)
  changes=$(git -C "$sdk" status --porcelain --ignore-submodules=all 2>/dev/null || true)
  if [[ $force -eq 0 ]]; then
    if [[ "$head" == "$expected_tree" && -z "$changes" ]]; then
      echo "Patched SDK at $sdk already (tree $head); --force replaces it"
      [[ $submodules -eq 0 ]] || git -C "$sdk" submodule update --init --recursive
      exit 0
    fi
    echo "$sdk exists but isn't the patched SDK (tree $head): run with --force to replace it" >&2
    exit 1
  fi
  rm -rf "$sdk"
fi
mkdir -p "$(dirname "$sdk")"

echo "Cloning ReXGlue from $source"
git clone --no-checkout "$source" "$sdk"
git -C "$sdk" checkout --quiet --detach "$base"

mapfile -t patches < <(find "$root/patches/rexglue-sdk-nx" -maxdepth 1 -name '*.patch' | sort)
[[ ${#patches[@]} -gt 0 ]] || { echo "No patches found in patches/rexglue-sdk-nx" >&2; exit 1; }
# The commits only need some identity; the checks compare source trees, not commits.
identity=(-c user.name='fear360-recomp setup' -c user.email=setup@localhost)
echo "Applying ${#patches[@]} patches"
# The patches are LF; a Windows checkout of this repository may have given them CRLF.
apply() { sed 's/\r$//' "$1" | git -C "$sdk" "${identity[@]}" am --quiet; }
apply "${patches[0]}"
if [[ "$(git -C "$sdk" rev-parse 'HEAD^{tree}')" != "$fear_nx_tree" ]]; then
  echo "The SDK tree after the fear-nx patch is not the expected one" >&2
  exit 1
fi
for patch in "${patches[@]:1}"; do apply "$patch"; done
tree=$(git -C "$sdk" rev-parse 'HEAD^{tree}')
if [[ "$tree" != "$expected_tree" ]]; then
  echo "The patched SDK tree is $tree, not $expected_tree: patches/rexglue-sdk-nx has changed" \
       "without this script, or a patch applied differently" >&2
  exit 1
fi
git -C "$sdk" switch --quiet -c fear360

if [[ $submodules -eq 1 ]]; then
  echo 'Fetching the SDK submodules (SDL, glslang, FFmpeg, ...)'
  git -C "$sdk" submodule update --init --recursive
fi
echo "Patched SDK ready at $sdk (tree $tree)."
