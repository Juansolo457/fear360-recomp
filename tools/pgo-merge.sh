#!/usr/bin/env bash
# Merge a PGO training run's counts into the profile the next build uses (#87): the .profraw
# files a FEAR_PGO=generate build wrote (pgo-raw/ in each game's folder, src/pgo.cpp) into
# pgo/<project>/default.profdata, which tools/build-linux.sh then uses by default.
#   tools/pgo-merge.sh --project port-nx    <folder or .profraw files>...
#   tools/pgo-merge.sh --project port-files <folder or .profraw files>...
# llvm-profdata must be the compiler's version: in the Deck build's container (as
# tools/build-deck.sh) when podman has its image, else the host's llvm-profdata-<clang major>.
# pgo/ stays out of git: the counts come from runs of the game's code.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
image=fear360-deck-build
project=""
inputs=()
while [[ $# -gt 0 ]]; do
  case "$1" in
    --project) project="$2"; shift ;;
    *) inputs+=("$1") ;;
  esac
  shift
done
case "$project" in
  port-nx|port-files) ;;
  *) echo "--project port-nx or port-files" >&2; exit 2 ;;
esac
files=()
for i in "${inputs[@]}"; do
  if [[ -d "$i" ]]; then
    for f in "$i"/*.profraw; do [[ -f "$f" ]] && files+=("$(readlink -f "$f")"); done
  elif [[ -f "$i" ]]; then
    files+=("$(readlink -f "$i")")
  fi
done
[[ ${#files[@]} -gt 0 ]] || { echo "no .profraw files in ${inputs[*]:-(nothing given)}" >&2; exit 1; }

out_dir="$root/pgo/$project"
mkdir -p "$out_dir"
if command -v podman >/dev/null && podman image exists "$image"; then
  # Each input mounted read-only at its own path, the output folder writable.
  mounts=(-v "$out_dir:/out")
  for f in "${files[@]}"; do mounts+=(-v "$f:$f:ro"); done
  podman run --rm --userns=keep-id "${mounts[@]}" "$image" \
    llvm-profdata-20 merge -o /out/default.profdata "${files[@]}"
else
  llvm=$("${CC:-clang}" -dumpversion | cut -d. -f1)
  tool=$(command -v "llvm-profdata-$llvm" || command -v llvm-profdata) || {
    echo "llvm-profdata is not installed" >&2; exit 1; }
  "$tool" merge -o "$out_dir/default.profdata" "${files[@]}"
fi
echo "Merged ${#files[@]} run(s) into pgo/$project/default.profdata"
