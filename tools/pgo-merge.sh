#!/usr/bin/env bash
# Merge a PGO training run's counts into the profile the next build uses (#87): the .profraw
# files a FEAR_PGO=generate build wrote (pgo-raw/ in each game's folder, src/pgo.cpp) into
# pgo/<project>/default.profdata, which tools/build-linux.sh then uses by default.
#   tools/pgo-merge.sh --project port-nx    <folder or .profraw files>...
#   tools/pgo-merge.sh --project port-files <folder or .profraw files>...
# llvm-profdata must be the version of the compiler that builds with the profile:
#   FEAR_PGO_PROFDATA=<tool>  that one, whatever else is installed
#   the Deck build's container's (as tools/build-deck.sh) when podman has its image: Deck builds
#   otherwise the host's llvm-profdata-<clang major>: builds made on this host
# The script says which it used.
# Needs python3 (tools/pgo_prepare.py).
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
work="$(mktemp -d)"
trap 'rm -rf "$work"' EXIT
# Each session's total (the last snapshot of a file older training builds appended to), merged,
# then without the functions the training never reached (tools/pgo_prepare.py has why).
python3 -I "$root/tools/pgo_prepare.py" last-snapshot "$work/raw" "${files[@]}"
if [[ -n "${FEAR_PGO_PROFDATA:-}" ]]; then
  tool=$(command -v "$FEAR_PGO_PROFDATA") || { echo "$FEAR_PGO_PROFDATA not found" >&2; exit 1; }
  echo "llvm-profdata: $tool (FEAR_PGO_PROFDATA)"
  profdata() { "$tool" "$@"; }
elif command -v podman >/dev/null && podman image exists "$image"; then
  echo "llvm-profdata: the Deck build's container (llvm-profdata-20), for Deck builds"
  profdata() {
    podman run --rm --userns=keep-id -v "$work:$work" -v "$out_dir:$out_dir" "$image" \
      llvm-profdata-20 "$@"
  }
else
  llvm=$("${CC:-clang}" -dumpversion | cut -d. -f1)
  # clang's own: llvm-profdata-<major>, or a plain llvm-profdata of that major version.
  tool=$(command -v "llvm-profdata-$llvm") || {
    tool=$(command -v llvm-profdata) &&
      [[ "$("$tool" --version | sed -nE 's/.*LLVM version ([0-9]+).*/\1/p' | head -1)" == "$llvm" ]]
  } || {
    echo "no llvm-profdata of clang $llvm's version: set FEAR_PGO_PROFDATA" >&2; exit 1; }
  echo "llvm-profdata: $tool, for builds on this host (clang $llvm)"
  profdata() { "$tool" "$@"; }
fi
profdata merge --text -o "$work/all.proftext" "$work"/raw/*.profraw
python3 -I "$root/tools/pgo_prepare.py" drop-unreached "$work/all.proftext" "$work/reached.proftext"
profdata merge -o "$out_dir/default.profdata" "$work/reached.proftext"
echo "Merged ${#files[@]} run(s) into pgo/$project/default.profdata"
