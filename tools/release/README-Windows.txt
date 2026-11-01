F.E.A.R. 360 Recompiled 1.5 for Windows
=======================================

The Xbox 360 version of F.E.A.R., statically recompiled to run natively on Windows: up to 4K and
120 fps, keyboard and mouse or a controller, and the console's own game code.

This package holds the game's program only. It contains no game data: you need your own Xbox 360
disc of F.E.A.R., and the game reads its files from it.

What you need
  - Windows 10 or 11, 64-bit
  - A CPU with AVX2 (Intel Haswell / AMD Excavator or newer)
  - A Direct3D 12 graphics card
  - Your own F.E.A.R. disc for Xbox 360, as a disc image (ISO). A PC drive can't read Xbox 360
    discs: https://github.com/Juansolo457/fear360-recomp/blob/master/docs/RIPPING.md
  - Python 3, to unpack the disc image (python.org)

Setup
  1. Extract this zip anywhere, for example C:\Games\FEAR360.
  2. Unpack your disc image into the fear\assets folder. In this folder, run:
       python -I xdvdfs.py "<your F.E.A.R. ISO>" extract fear\assets
     fear\assets should then hold default.xex and a Game folder.
  3. Double-click "Play F.E.A.R..cmd".

The first start asks whether you play with the keyboard and mouse or a controller. Change it later
in Recompilation Settings in the main menu, along with the resolution, frame rate, field of view and
the rest.

Only this version of the disc is supported (default.xex SHA-256):
  e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792

PC textures and sharper text
  Recompilation Settings > PC textures uses the PC version's textures and redraws the game's text
  sharply. You build them from your own copy of the PC game with the builder in texture-pack/
  (Python 3 with numpy and Pillow); the steps:
  https://github.com/Juansolo457/fear360-recomp/blob/master/docs/TEXTURES.md#from-a-release-zip

The first start builds shaders as they first appear (brief hitches the first time an area or
effect is seen); they are kept in fear\cache, so later starts are smooth.

Saves and settings: fear\user. Logs: fear\logs, one per run. Report problems on GitHub with the
newest log: https://github.com/Juansolo457/fear360-recomp/issues
