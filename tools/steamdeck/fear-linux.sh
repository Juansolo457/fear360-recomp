#!/bin/bash
# F.E.A.R. 360 recomp - the native Linux build on the Steam Deck: no Proton. As GRAW 360's
# graw-linux.sh. Sits at the top of the FEAR360-SteamDeck folder, next to fear/ and fearfiles/
# (each with its Linux program, assets/ and user/ as in the Proton package).
# Starts the collection menu in F.E.A.R. (fear/fear); Switch Game starts F.E.A.R. Files
# (fearfiles/fearfiles) and back, through collection-linux.toml, which this script writes.
#   FEAR_FPS=120      the frame rate (default 60): a timer at twice it paces the game
#   FEAR_START=files  skip the menu and start F.E.A.R. Files (fear: F.E.A.R.)
# Extra arguments go to the game, e.g.  ./fear-linux.sh --no-fullscreen
here="$(cd "$(dirname "$0")" && pwd)"
fps="${FEAR_FPS:-60}"
cd "$here/fear" || { echo "No fear folder next to $0" >&2; exit 1; }
mkdir -p logs
stamp="$(date +%Y%m%d-%H%M%S)"
info="$here/fear/logs/system-$stamp.txt"

# The settings each game starts with; the collection passes them on when it switches games.
# A command-line flag beats user/settings.toml (the Proton package's 120 fps timer, for one).
args=(--resolution 720p --anisotropic_override 5 --game_data_root assets --user_data_root user
      --cache_root cache --fear_vblank_source timer --video_mode_refresh_rate "$((fps * 2))")
{
  echo "# The F.E.A.R. collection menu, native Linux: written by fear-linux.sh at each start."
  for game in "fear|fear|F.E.A.R.|First Encounter Assault Recon" \
              "files|fearfiles|F.E.A.R. Files|Extraction Point and Perseus Mandate"; do
    IFS='|' read -r id folder name subtitle <<< "$game"
    printf "\n[[title]]\nid = '%s'\nname = '%s'\nsubtitle = '%s'\n" "$id" "$name" "$subtitle"
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
  echo "fps: $fps"
  for program in "$here/fear/fear" "$here/fearfiles/fearfiles"; do
    echo "--- ldd $program"
    ldd "$program" 2>&1
  done
} > "$info"

# An unzip that dropped the executable bits.
chmod +x "$here/fear/fear" "$here/fearfiles/fearfiles" 2>/dev/null
if [ ! -f assets/default.xex ]; then
  echo "No fear/assets/default.xex: unpack this over the FEAR360-SteamDeck folder" | tee -a "$info" >&2
  exit 1
fi

program=fear start=()
if [ "$FEAR_START" = files ]; then
  program=fearfiles start=(--fear_collection_start files)
  cd "$here/fearfiles" && mkdir -p logs
elif [ -n "$FEAR_START" ]; then
  start=(--fear_collection_start fear)
fi
"./$program" "${args[@]}" --fear_collection ../collection-linux.toml "${start[@]}" "$@" \
  >> "$here/fear/logs/stdout-$stamp.txt" 2>&1
status=$?
# The first program's: after Switch Game, the other one keeps running on its own.
echo "exit status ($program): $status" >> "$info"
exit $status
