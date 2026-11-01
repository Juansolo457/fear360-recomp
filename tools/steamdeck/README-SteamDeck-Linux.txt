F.E.A.R. 360 Recompiled: native Linux build for the Steam Deck
==============================================================

The Xbox 360 F.E.A.R. built for Linux: no Proton, no Wine. It draws with the renderer's Vulkan
backend straight on the Deck's GPU driver. This package holds everything to play: the game's
program, your game data (from your own disc) and its starting settings. It contains your game data
and a program made from your disc: keep it to yourself.

F.E.A.R. Files (Extraction Point and Perseus Mandate) comes in a later release: the menu lists it
as coming later.

What's in it (FEAR360-SteamDeck/)
  fear-linux.sh                   the launcher: the collection menu, logs, the frame rate
  fear/                           the Linux program and .so libraries, assets/ (the game data) and
                                  user/ (saves and settings.toml)
  README-SteamDeck-Linux.txt      this file

Setup (Desktop Mode)
  1. Copy the zip to the Deck and extract it where you keep games (e.g. /home/deck/Games). About
     4 GB once extracted. (Over an earlier copy: your saves stay, but the settings go back to the
     baseline below. A programs-only update zip leaves the settings alone.)
  2. Steam > Games > "Add a Non-Steam Game to My Library" > Browse > set the file type to
     "All Files" > FEAR360-SteamDeck/fear-linux.sh > Add.
  3. In the library, right-click the new entry > Properties:
       - Compatibility: leave "Force the use of a specific Steam Play compatibility tool"
         UNTICKED. This is a Linux program; Proton can't run it.
       - Shortcut > LAUNCH OPTIONS: leave empty (the frame rate is in Recompilation Settings).
  4. Rename it (e.g. "F.E.A.R.") and go back to Game Mode.

It starts at the collection menu; pick F.E.A.R. The controller comes through Steam Input, as for
any non-Steam game.

Settings: Recompilation Settings in the game's main menu. It starts at the Deck baseline: the
360's settings (field of view 70, no bloom) at up to 120 fps, with MSAA off.

The frame rate: Recompilation Settings > Frame rate limit, 120 fps until changed there (Save and
restart applies it). Mailbox presentation: no tearing; frames beyond the screen's refresh are dropped. The Deck's
own screen shows up to 90. Docked, a TV at 60 Hz shows 60 even with 120 set; for 120, put the TV
in its 120 Hz mode (a "game" or "PC" input on most TVs) and pick 120 Hz in Steam's display
settings. 120 Hz over the dock needs a cable and TV port for HDMI 2.1; if the picture drops out
at 120 Hz, try 60 Hz.

Launch options (Properties > Shortcut > LAUNCH OPTIONS):
  FEAR_FPS=60 %command%      this frame rate instead of Recompilation Settings'

The first run builds shaders and pipelines as they appear (brief hitches the first time an area
or effect is seen); they're kept in the game's cache/ folder, so later runs are smooth.

Logs
  fear/logs/fear_NNN.log        the game's log, one per run
  fear/logs/system-<time>.txt   SteamOS, glibc and libstdc++ versions and the libraries found
  fear/logs/stdout-<time>.txt   anything the program printed outside its log (a crash's last words)

If it doesn't start
  - Check Compatibility is unticked.
  - Read fear/logs/system-<time>.txt: a "LACKS GLIBCXX" or "not found" line names the problem.
  - Report it on GitHub with the newest files from fear/logs/.
