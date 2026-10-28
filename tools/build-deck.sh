#!/usr/bin/env bash
# Build F.E.A.R. or F.E.A.R. Files for the Steam Deck: tools/setup-sdk.sh and tools/build-linux.sh
# inside the Ubuntu 22.04 container of tools/linux/deck.Containerfile (podman), so the game needs
# glibc 2.35 at most; a build made on the host needs the host's glibc, which SteamOS may not have.
# The container's SDK checkout and build folders are its own (third_party/rexglue-sdk-nx-deck,
# <project>/out/build/deck-amd64-<config>, -nolto without ThinLTO), so host builds in the same tree
# are left alone. Then every binary is checked for its glibc requirement. As GRAW 360's
# tools/build-deck.sh.
#   tools/build-deck.sh                          # F.E.A.R.
#   tools/build-deck.sh --project port-files --jobs 12
#   tools/build-deck.sh --rebuild-image          # rebuild the container image first
# Release by default (--config to change): RelWithDebInfo also compiles in the SDK's Tracy zones
# and perf counters (one atomic add per guest indirect call), and -O2 for the translated code:
# the heaviest Deck fights ran 18.3 ms a frame against Release's 15.1 (game thread bound).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=fear360-deck-build
glibc_max=2.35
project=port-nx
config=Release
jobs=$(( $(nproc) > 2 ? $(nproc) - 2 : 1 ))
rebuild=0
while [[ $# -gt 0 ]]; do
  case "$1" in
    --project) project="$2"; shift ;;
    --config) config="$2"; shift ;;
    --jobs) jobs="$2"; shift ;;
    --rebuild-image) rebuild=1 ;;
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
command -v podman >/dev/null || { echo "podman is not installed (docs/SETUP.md, Linux)" >&2; exit 1; }

if [[ $rebuild -eq 1 ]] || ! podman image exists "$image"; then
  podman build -t "$image" -f "$root/tools/linux/deck.Containerfile" "$root/tools/linux"
fi

sdk=/src/third_party/rexglue-sdk-nx-deck
build_rel="$project/out/build/deck-amd64-${config,,}"
# ThinLTO as tools/build-linux.sh (on, except Debug); FEAR_LTO=OFF builds without it, in a build
# folder of its own.
lto="${FEAR_LTO:-}"
[[ -n "$lto" ]] || { [[ "$config" == Debug ]] && lto=OFF || lto=ON; }
[[ "$lto" == ON || "$config" == Debug ]] || build_rel="$build_rel-nolto"
# The disc files: a project's assets may be a link out of the tree; the link's target is mounted
# read-only at the same path so it resolves.
mounts=(-v "$root:/src")
for assets in "$root"/port*/assets; do
  [[ -e "$assets" ]] || continue
  real="$(readlink -f "$assets")"
  case "$real" in "$root"/*) continue ;; esac
  # Once each: the projects' links may share one folder.
  [[ " ${mounts[*]} " == *" $real:$real:ro "* ]] || mounts+=(-v "$real:$real:ro")
done
podman run --rm --userns=keep-id "${mounts[@]}" -w /src -e HOME=/tmp \
  -e FEAR_SDK_DIR="$sdk" -e FEAR_BUILD_DIR="/src/$build_rel" -e FEAR_LTO="$lto" "$image" \
  bash -c "tools/setup-sdk.sh && tools/build-linux.sh --project $project --config $config --jobs $jobs"

# The newest glibc symbol version each binary needs, and libstdc++'s for the record (libstdc++ 13
# here: GLIBCXX_3.4.32 at most, which the Deck's libstdc++ has to provide). Read with the
# container's llvm-readelf: binutils' objdump stops at lld's large executables.
dynsyms() {
  podman run --rm -v "$root:/src:ro" "$image" \
    llvm-readelf-20 --dyn-syms --wide "/src/${1#"$root"/}" 2>/dev/null || true
}
fail=0
for f in "$root/$build_rel"/*; do
  [[ -f "$f" && ( -x "$f" || "$f" == *.so ) ]] || continue
  file -b "$f" | grep -q '^ELF' || continue
  # (grep finds nothing in a binary without these symbol versions: not an error.)
  syms=$(dynsyms "$f")
  need=$({ grep -o 'GLIBC_[0-9.]*' || true; } <<< "$syms" | sed 's/GLIBC_//' | sort -uV | tail -1)
  cxx=$({ grep -o 'GLIBCXX_[0-9.]*' || true; } <<< "$syms" | sort -uV | tail -1)
  if [[ -n "$need" && "$(printf '%s\n%s\n' "$need" "$glibc_max" | sort -V | tail -1)" != "$glibc_max" ]]; then
    echo "$(basename "$f"): needs glibc $need, above $glibc_max" >&2
    fail=1
  else
    echo "$(basename "$f"): glibc ${need:-none}${cxx:+, $cxx}"
  fi
done
[[ $fail -eq 0 ]] || exit 1
echo "Deck build: $build_rel (LTO $lto)"
