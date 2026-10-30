#!/bin/bash
# F.E.A.R. 360 recomp - the native Linux build on the Steam Deck: no Proton. As GRAW 360's
# graw-linux.sh. Sits at the top of the FEAR360-SteamDeck folder, next to fear/ (the Linux
# program, assets/ and user/).
# Starts the collection menu in F.E.A.R. (fear/fear), through collection-linux.toml, which this
# script writes. 1.0 is F.E.A.R. alone: the menu lists F.E.A.R. Files as coming later.
#   FEAR_FPS=60       the frame rate for this start (default: PC Settings' Frame rate, 120 until
#                     changed there); a timer at twice it paces the game
#   FEAR_GOD=1        the player can't die (fear_invulnerable: the smoke test, #55)
# Development, with a fearfiles/ folder (make_package.py --linux --files):
#   FEAR_FILES=1      list F.E.A.R. Files as a game to play; Switch Game goes between the two
#   FEAR_START=files  skip the menu and start F.E.A.R. Files (fear: F.E.A.R.)
# Extra arguments go to the game, e.g.  ./fear-linux.sh --no-fullscreen
here="$(cd "$(dirname "$0")" && pwd)"
fps="${FEAR_FPS:-}"
cd "$here/fear" || { echo "No fear folder next to $0" >&2; exit 1; }
mkdir -p logs
stamp="$(date +%Y%m%d-%H%M%S)"
info="$here/fear/logs/system-$stamp.txt"

# The settings each game starts with; the collection passes them on when it switches games.
# A command-line flag beats user/settings.toml. No timer flags: with no display vblank to wait
# on (Linux), the game runs its timer at twice PC Settings' frame rate, so changing it there works
# (pinned to 240 Hz here, every other choice came back at 30 fps after Save and restart).
args=(--resolution 720p --anisotropic_override 5 --game_data_root assets --user_data_root user
      --cache_root cache --no-vulkan_allow_present_mode_immediate)
[ -n "$fps" ] && args+=(--fear_frame_rate_limit "$fps")
[ "$FEAR_GOD" = 1 ] && args+=(--fear_invulnerable true)
# (Mailbox presentation, as the Proton package: frames beyond the screen's refresh are dropped,
# no tearing. ReXGlue's presenter takes immediate first when it may.)
# An unzip that dropped the executable bits.
chmod +x "$here/fear/fear" "$here/fearfiles/fearfiles" 2>/dev/null
files_playable=0
if [ "$FEAR_FILES" = 1 ] || [ "$FEAR_START" = files ]; then
  [ -x "$here/fearfiles/fearfiles" ] && files_playable=1
fi
{
  echo "# The F.E.A.R. collection menu, native Linux: written by fear-linux.sh at each start."
  for game in "fear|fear|F.E.A.R.|First Encounter Assault Recon" \
              "files|fearfiles|F.E.A.R. Files|Extraction Point and Perseus Mandate"; do
    IFS='|' read -r id folder name subtitle <<< "$game"
    printf "\n[[title]]\nid = '%s'\nname = '%s'\nsubtitle = '%s'\n" "$id" "$name" "$subtitle"
    if [ "$id" = files ] && [ "$files_playable" != 1 ]; then
      printf "later = true\n"
      continue
    fi
    printf "exe = '../%s/%s'\nworking_dir = '../%s'\nargs = [" "$folder" "$folder" "$folder"
    sep=""
    for a in "${args[@]}"; do printf "%s'%s'" "$sep" "$a"; sep=", "; done
    printf "]\n"
  done
} > "$here/collection-linux.toml"

# What the logs need to explain a failed start.
{
  echo "date: $(date)"
  grep -E '^(PRETTY_NAME|VERSION_ID|BUILD_ID)=' /etc/os-release 2>/dev/null
  echo "kernel: $(uname -r)"
  echo "glibc: $(ldd --version 2>/dev/null | head -1)"
  libstdcxx="$(ldconfig -p 2>/dev/null | grep -m1 'libstdc++.so.6 ' | sed 's/.*=> //')"
  [ -z "$libstdcxx" ] && libstdcxx=/usr/lib/libstdc++.so.6
  if grep -aq GLIBCXX_3.4.32 "$libstdcxx" 2>/dev/null; then
    echo "libstdc++: $libstdcxx has GLIBCXX_3.4.32 (needed)"
  else
    echo "libstdc++: $libstdcxx LACKS GLIBCXX_3.4.32 (needed): the game can't start"
  fi
  echo "display: DISPLAY=$DISPLAY WAYLAND_DISPLAY=$WAYLAND_DISPLAY"
  echo "fps: ${fps:-PC Settings}"
  # The controllers: what Steam Input tells SDL, and the input devices the game can open.
  echo "--- controller environment"
  env | grep -E '^(SDL_|SteamVirtualGamepadInfo|SteamAppId|SteamGameId|SteamDeck)' | cut -c1-300
  echo "--- /dev/input"
  ls -l /dev/input 2>&1
  for program in "$here/fear/fear" "$here/fearfiles/fearfiles"; do
    [ -e "$program" ] || continue
    echo "--- ldd $program"
    ldd "$program" 2>&1
  done
} > "$info"

program=fear start=()
if [ "$FEAR_START" = files ]; then
  program=fearfiles start=(--fear_collection_start files)
  cd "$here/fearfiles" || { echo "No fearfiles folder next to $0" | tee -a "$info" >&2; exit 1; }
  mkdir -p logs
elif [ -n "$FEAR_START" ]; then
  start=(--fear_collection_start fear)
fi
if [ ! -f assets/default.xex ]; then
  echo "No $program/assets/default.xex: unpack this over the FEAR360-SteamDeck folder" | tee -a "$info" >&2
  exit 1
fi
"./$program" "${args[@]}" --fear_collection ../collection-linux.toml "${start[@]}" "$@" \
  >> "$here/fear/logs/stdout-$stamp.txt" 2>&1
status=$?
# The first program's: after Switch Game, the other one keeps running on its own.
echo "exit status ($program): $status" >> "$info"
exit $status
