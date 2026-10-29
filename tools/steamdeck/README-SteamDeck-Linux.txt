F.E.A.R. 360 recomp (F.E.A.R. + F.E.A.R. Files): native Linux build for the Steam Deck
====================================================================================

The games built for Linux: no Proton, no Wine. They draw with the renderer's Vulkan backend
straight on the Deck's GPU driver. This package holds everything to play: both games' programs,
your game data (from your own discs) and their starting settings. It contains your game data and
programs made from your discs: keep it to yourself.

What's in it (FEAR360-SteamDeck/)
  fear-linux.sh                   the launcher: the collection menu, logs, the frame rate
  fear/, fearfiles/               each game's Linux program and .so libraries, assets/ (the game
                                  data) and user/ (saves and settings.toml)
  README-SteamDeck-Linux.txt      this file

Setup (Desktop Mode)
  1. Copy the zip to the Deck and extract it where you keep games (e.g. /home/deck/Games). About
     11 GB once extracted. (Updating an earlier copy: extract over it; your saves stay. A
     programs-only update zip leaves your settings alone too.)
  2. Steam > Games > "Add a Non-Steam Game to My Library" > Browse > set the file type to
     "All Files" > FEAR360-SteamDeck/fear-linux.sh > Add.
  3. In the library, right-click the new entry > Properties:
       - Compatibility: leave "Force the use of a specific Steam Play compatibility tool"
         UNTICKED. This is a Linux program; Proton can't run it.
       - Shortcut > LAUNCH OPTIONS: leave empty (the frame rate is in PC Settings).
  4. Rename it (e.g. "F.E.A.R. Collection") and go back to Game Mode.

It starts at the collection menu; pick a game. Switch Game in either game's main menu starts the
other one. The controller comes through Steam Input, as for any non-Steam game.

Settings: PC Settings in each game's main menu. Both start at the Deck baseline: the 360's
settings (field of view 70, no bloom) at up to 120 fps, with MSAA off. Each game keeps its own.

The frame rate: PC Settings > Frame rate, 120 fps until changed there (Save and restart applies
it). Mailbox presentation: no tearing; frames beyond the screen's refresh are dropped. The Deck's
own screen shows up to 90. Docked, a TV at 60 Hz shows 60 even with 120 set; for 120, put the TV
in its 120 Hz mode (a "game" or "PC" input on most TVs) and pick 120 Hz in Steam's display
settings. 120 Hz over the dock needs a cable and TV port for HDMI 2.1; if the picture drops out
at 120 Hz, try 60 Hz.

Launch options (Properties > Shortcut > LAUNCH OPTIONS), for both games:
  FEAR_FPS=60 %command%      this frame rate instead of PC Settings'
  FEAR_START=files %command% start F.E.A.R. Files without the menu
  FEAR_GOD=1 %command%       the player can't die (for testing)

The first run builds shaders and pipelines as they appear (brief hitches the first time an area
or effect is seen); they're kept in each game's cache/ folder, so later runs are smooth.

Logs
  fear/logs/fear_NNN.log, fearfiles/logs/fear_NNN.log   the games' logs, one per run
  fear/logs/system-<time>.txt   SteamOS, glibc and libstdc++ versions and the libraries found
  fear/logs/stdout-<time>.txt   anything the program printed outside its log (a crash's last words)

If it doesn't start
  - Check Compatibility is unticked.
  - Read fear/logs/system-<time>.txt: a "LACKS GLIBCXX" or "not found" line names the problem.
  - Report it on GitHub with the newest files from fear/logs/.
