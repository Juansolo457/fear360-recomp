#!/usr/bin/env bash
# Unattended check on Linux (WSL's WSLg or a desktop), the counterpart of tools/autoplay.ps1:
# launch our own windowed Linux build with keyboard-as-controller (--mnk_mode) on X11, tap a key
# at an interval (Space = A: through "Press Start", the profile prompt, New game, difficulty and
# the intro), take screenshots on a schedule, then stop it. Its own profile and cache.
# Needs xdotool and ImageMagick's import.
#   tools/autoplay-linux.sh --seconds 300 --tag wsl
#   tools/autoplay-linux.sh --files --build deck-amd64-relwithdebinfo --extra '--fear_collection x.toml'
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
seconds=150 key_every=2 key=space then_key="" keys_until=0 capture_every=15 tag=run extra=""
project=port-nx program=fear build=""
while [[ $# -gt 0 ]]; do
  case "$1" in
    --seconds) seconds="$2"; shift ;;
    --key-every) key_every="$2"; shift ;;
    --key) key="$2"; shift ;;
    --keys-until) keys_until="$2"; shift ;;   # then tap --then-key instead (0 = never)
    --then-key) then_key="$2"; shift ;;       # e.g. e = right trigger
    --capture-every) capture_every="$2"; shift ;;
    --tag) tag="$2"; shift ;;
    --extra) extra="$2"; shift ;;
    --files) project=port-files program=fearfiles ;;
    --build) build="$2"; shift ;;             # the build folder (default linux-amd64-relwithdebinfo)
    *) echo "unknown option $1" >&2; exit 2 ;;
  esac
  shift
done
for cmd in xdotool import; do
  command -v "$cmd" >/dev/null || { echo "$cmd is not installed" >&2; exit 1; }
done
port="$root/$project"
exe="$port/out/build/${build:-linux-amd64-relwithdebinfo}/$program"
[[ -x "$exe" ]] || { echo "$exe is missing" >&2; exit 1; }
stamp="$(date +%Y%m%d-%H%M%S)"
mkdir -p "$port/logs"
log="$port/logs/autoplay-$tag-$stamp.log"
captures="$root/private/captures/autoplay-$tag-$stamp"
mkdir -p "$captures"

# X11, so xdotool can find and drive the window.
cd "$port"
# shellcheck disable=SC2086
GDK_BACKEND=x11 SDL_VIDEODRIVER=x11 "$exe" --no-fullscreen --mnk_mode --resolution 720p --headless \
  --game_data_root "$port/assets" --user_data_root "$port/user-smoke" \
  --cache_root "$port/cache-smoke" --log_file "$log" $extra \
  > "$captures/stdout.txt" 2>&1 &
pid=$! child=$!
trap 'kill "$pid" 2>/dev/null || true' EXIT

# The collection's Switch Game starts the other program and the first one exits: follow the
# newest fear or fearfiles process started after it, so input, screenshots and the stop at the
# end go to the game that is running.
replacement() {
  local p
  for _ in $(seq 20); do
    p=$(pgrep -n -x 'fear|fearfiles' 2>/dev/null || true)
    if [[ -n "$p" && "$p" != "$1" ]]; then echo "$p"; return 0; fi
    sleep 0.25
  done
  return 1
}

start=$(date +%s)
next_key=5 next_capture=$capture_every window="" switches=0 status=""
while true; do
  elapsed=$(( $(date +%s) - start ))
  (( elapsed < seconds )) || break
  if ! kill -0 "$pid" 2>/dev/null; then
    # Only the program this script started is its child: a replacement's exit code is unknown.
    code=unknown
    if [[ "$pid" == "$child" ]]; then wait "$pid" && code=0 || code=$?; fi
    if [[ "$code" != 0 && "$code" != unknown ]]; then
      status="exited early ($code)"
      break
    fi
    if next=$(replacement "$pid"); then
      echo "switched games at $elapsed s: pid $pid -> $next" >&2
      pid=$next window="" switches=$(( switches + 1 ))
      continue
    fi
    status="exited early ($code)"
    break
  fi
  if [[ -z "$window" ]]; then
    window=$(xdotool search --pid "$pid" --onlyvisible 2>/dev/null | tail -1 || true)
  fi
  if [[ -n "$window" ]] && (( elapsed >= next_key )); then
    k="$key"
    if (( keys_until > 0 && elapsed >= keys_until )); then k="$then_key"; fi
    if [[ -n "$k" ]]; then
      xdotool windowactivate --sync "$window" 2>/dev/null || true
      xdotool keydown --window "$window" "$k" 2>/dev/null || true
      sleep 0.08
      xdotool keyup --window "$window" "$k" 2>/dev/null || true
    fi
    next_key=$(( next_key + key_every ))
  fi
  if [[ -n "$window" ]] && (( elapsed >= next_capture )); then
    import -window "$window" "$captures/$(printf '%04d' "$elapsed").png" 2>/dev/null ||
      echo "capture at $elapsed s failed" >&2
    next_capture=$(( next_capture + capture_every ))
  fi
  sleep 0.25
done
if [[ -z "$status" ]]; then
  kill "$pid" 2>/dev/null || true
  sleep 2
  kill -9 "$pid" 2>/dev/null || true
  status=stopped
fi
trap - EXIT
echo "status=$status switches=$switches log=$log captures=$captures"
# A game that ended before the time was up (a crash, a failed start) fails the run.
[[ "$status" == stopped ]] || exit 1
