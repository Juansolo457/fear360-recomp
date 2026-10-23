F.E.A.R. 360 recomp (F.E.A.R. + F.E.A.R. Files): Steam Deck test build
=========================================================================

These are the Windows builds, run on the Deck through Proton. They talk to Vulkan directly.
The package is made from the PC's current builds (tools/steamdeck/make_package.py in the repo),
with your saves and the PC texture packs.

Folders
  fear/              F.E.A.R. (program, game data in assets/, saves in user/, PC textures)
  fearfiles/         F.E.A.R. Files (the same layout)
  collection.toml    the collection menu (Switch Game between the two)

Setup (Desktop Mode)
  1. Extract this zip somewhere on the Deck, e.g. /home/deck/Games/FEAR360-SteamDeck
     (Dolphin: right-click the zip > Extract > Extract archive here).
  2. Steam > Games > "Add a Non-Steam Game to My Library" > Browse > fear/fear.exe > Add.
  3. In the library, right-click the new entry > Properties:
       - Compatibility: tick "Force the use of a specific Steam Play compatibility tool",
         pick Proton Experimental (or the newest Proton 9/10).
       - Shortcut > START IN: must be the fear folder, e.g.
           "/home/deck/Games/FEAR360-SteamDeck/fear/"
         (Steam fills this in; check it).
       - Shortcut > LAUNCH OPTIONS: paste this one line:

WINEDLLOVERRIDES="msvcp140,msvcp140_atomic_wait,vcruntime140,vcruntime140_1=n,b" %command% --resolution 720p --anisotropic_override 5 --game_data_root assets --user_data_root user --cache_root cache --nx_renderer native --fear_collection ../collection.toml

     Nothing else: the Deck's settings are in each game's user/settings.toml (1x render scale,
     uncapped up to 120 fps, no MSAA, SDL's HIDAPI drivers off, the profiler on for the logs).
     The package has no PC texture pack; the baseline is the 360's own textures.

  4. Rename the entry (e.g. "F.E.A.R. Collection") and go back to Game Mode.

It starts at the collection menu; pick a game. Switch Game in either game's main menu starts the
other one. Settings are in each game's PC Settings menu (render scale is set to 1x here for the
Deck's 1280x800 screen).

Performance testing
  - The Deck's overlay: Quick Access (...) > Performance > Performance Overlay Level 3 or 4.
  - This package runs uncapped up to 120 fps: the game paces itself from a 240 Hz timer, not the
    display, and on the Deck's screen the frames beyond its refresh are dropped (mailbox, no
    tearing). The overlay's fps is the game's. Leave the Deck's Framerate Limit off: the game
    can't see it, and a limit below the game's pace made frames uneven (first Deck test, #28).
    For 60 again, see the comment in user/settings.toml.
  - The first run translates shaders as they appear (brief hitches the first time an effect is
    seen); each later run starts with them pre-built, so test the second run for smoothness.
  - Saving PC Settings in the game rewrites user/settings.toml with only the panel's settings:
    MSAA, the HIDAPI setting and the profiler then go back to their defaults.
  - Logs: fear/logs/fear_NNN.log and fearfiles/logs/fearfiles_NNN.log (one per run). Every 10 s
    they give frame pacing, renderer CPU/GPU time per frame and slow frames. Copy them back to
    the PC for a read.

Diagnosis (add to the end of the launch options, one run at a time; a flag beats settings.toml)
  On/off settings are switched on with --name and off with --no-name (or --name=false).
  "--name false" does NOT turn one off: the parser reads --name as on and skips the "false".
  - The profiler is on in this package (user/settings.toml); off: --no-fear_profile
    The log has no "[profile] sampling busy threads" line when it is off.
  - The corrupted particles/hands (#28) are fixed by exact vertex checks, now the default.
    To compare: the older checks:                           --no-fear_vertex_fresh
    no partial vertex reads (#27):                          --no-fear_vertex_spans
    The log's "[native] vertex reads:" line confirms the setting.
  - MSAA back on (the game's own 2x/4x):                    --nx_native_msaa
  - SDL's direct HID drivers back on (#29):                 --hid_sdl_hidapi on
    The log's "SDL: HIDAPI joystick drivers" line confirms it.

If it doesn't start
  - Make sure START IN is the fear folder (the paths in the launch options are relative to it).
  - Try another Proton version.
  - Send the newest log from fear/logs/.
