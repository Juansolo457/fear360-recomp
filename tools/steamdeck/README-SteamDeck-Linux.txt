F.E.A.R. 360 recomp (F.E.A.R. + F.E.A.R. Files): native Linux test build for the Steam Deck
=========================================================================================

The games built for Linux: no Proton, no Wine. They draw with the D3D12 renderer's Vulkan
backend straight on the Deck's GPU driver. This zip has only the programs: unpack it over the
FEAR360-SteamDeck folder from the Proton package, which keeps the game data (assets/), your saves
and settings (user/). The Proton programs (fear.exe and the DLLs) stay and still work.

What it adds
  fear-linux.sh                 the launcher: the collection menu, logs, the frame rate
  fear/fear, fearfiles/fearfiles  the Linux programs, each with its .so libraries
  README-SteamDeck-Linux.txt    this file

Setup (Desktop Mode)
  1. Extract this zip into the folder that holds FEAR360-SteamDeck (e.g. /home/deck/Games), so
     its FEAR360-SteamDeck folder merges with yours. Overwrite when asked.
  2. Steam > Games > "Add a Non-Steam Game to My Library" > Browse > set the file type to
     "All Files" > FEAR360-SteamDeck/fear-linux.sh > Add.
  3. In the library, right-click the new entry > Properties:
       - Compatibility: leave "Force the use of a specific Steam Play compatibility tool"
         UNTICKED. This is a Linux program; Proton can't run it.
       - Shortcut > LAUNCH OPTIONS: leave empty (the frame rate is in PC Settings).
  4. Rename it (e.g. "F.E.A.R. Collection (Linux)") and go back to Game Mode.

It starts at the collection menu; pick a game. Switch Game in either game's main menu starts the
other one. The controller comes through Steam Input, as for any non-Steam game.

The frame rate: PC Settings > Frame rate in each game's main menu, 120 fps until changed there
(Save and restart applies it). Mailbox presentation: no tearing; on the Deck's screen the frames
beyond its refresh are dropped. FEAR_FPS=60 %command% in the launch options overrides the setting
in both games. FEAR_START=files starts F.E.A.R. Files without the menu.
Each game keeps its own setting: set both if you want both at 120.

The screen sets the ceiling: no game shows more frames than the display refreshes. The Deck's
own screen shows up to 90. Docked, a TV at 60 Hz shows 60 even with 120 set; for 120, put the TV
in its 120 Hz mode (a "game" or "PC" input on most TVs) and pick 120 Hz in Steam's display
settings. 120 Hz over the dock needs a cable and TV port for HDMI 2.1; if the picture drops out
at 120 Hz, try 60 Hz.

What to test
  - The collection menu: does the controller move and pick? Switch Game both ways.
  - The guns: fully drawn, not see-through (the Deck's depth buffer is 32-bit float, unlike the
    PC's; this is the first test of that path).
  - Shimmer on edges and distant detail, compared with the Proton build.
  - Audio, saving and loading, suspend and resume.
  - The frame rate in a fight (Performance Overlay level 3 or 4): 120, 60 and 30 from PC Settings.
  - The first run builds shaders as they appear (brief hitches); test the second run for
    smoothness. The Linux cache is separate from Proton's (cache/fear-d3d12-vulkan).

Logs (copy them back to the PC)
  fear/logs/fear_NNN.log, fearfiles/logs/fear_NNN.log   the games' logs, one per run
  fear/logs/system-<time>.txt   SteamOS, glibc and libstdc++ versions and the libraries found
  fear/logs/stdout-<time>.txt   anything the program printed outside its log (a crash's last words)

If it doesn't start
  - Check Compatibility is unticked.
  - Read fear/logs/system-<time>.txt: a "LACKS GLIBCXX" or "not found" line names the problem.
  - Send the newest files from fear/logs/.
