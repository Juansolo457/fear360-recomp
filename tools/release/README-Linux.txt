F.E.A.R. 360 Recompiled 1.5 for Linux and the Steam Deck
========================================================

The Xbox 360 version of F.E.A.R., statically recompiled to run natively on Linux: no Proton, no
Wine. It draws with Vulkan straight on the GPU driver, at up to 120 fps.

This package holds the game's program only. It contains no game data: you need your own Xbox 360
disc of F.E.A.R., and the game reads its files from it.

What you need
  - A Steam Deck, or an x86-64 Linux PC with a Vulkan 1.3 GPU and a CPU with AVX2 and FMA
  - Your own F.E.A.R. disc for Xbox 360, as a disc image (ISO). A PC drive can't read Xbox 360
    discs: https://github.com/Juansolo457/fear360-recomp/blob/master/docs/RIPPING.md
  - Python 3, to unpack the disc image (SteamOS has it)

Setup (on the Deck: Desktop Mode)
  1. Extract this zip where you keep games, for example /home/deck/Games.
  2. Unpack your disc image into the fear/assets folder. In this folder, run:
       python3 -I xdvdfs.py "<your F.E.A.R. ISO>" extract fear/assets
     fear/assets should then hold default.xex and a Game folder.
  3. Run fear-linux.sh. On the Deck, add it to Steam instead:
       - Steam > Games > "Add a Non-Steam Game to My Library" > Browse > set the file type to
         "All Files" > fear-linux.sh > Add.
       - Right-click the new entry > Properties > Compatibility: leave "Force the use of a
         specific Steam Play compatibility tool" UNTICKED. This is a Linux program.
       - Rename it (e.g. "F.E.A.R.") and go back to Game Mode.

On the Deck the controls are the Deck's. On a Linux desktop the first start asks whether you play
with the keyboard and mouse or a controller. Change it later in Recompilation Settings in the main
menu, along with the frame rate, field of view and the rest. It starts at the Deck baseline: the
360's settings at up to 120 fps, with anti-aliasing off.

Only this version of the disc is supported (default.xex SHA-256):
  e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792

PC textures and sharper text
  Recompilation Settings > PC textures uses the PC version's textures and redraws the game's text
  sharply. They are built from your own copy of the PC game:
  https://github.com/Juansolo457/fear360-recomp/blob/master/docs/TEXTURES.md

Launch options (Steam: Properties > Shortcut > LAUNCH OPTIONS):
  FEAR_FPS=60 %command%      this frame rate instead of Recompilation Settings'

The first run builds shaders as they first appear (brief hitches the first time an area or effect
is seen); they are kept in fear/cache, so later runs are smooth.

Logs: fear/logs. If it doesn't start, read fear/logs/system-<time>.txt (a "LACKS GLIBCXX" or "not
found" line names the problem) and report it on GitHub with the newest files from fear/logs:
https://github.com/Juansolo457/fear360-recomp/issues
