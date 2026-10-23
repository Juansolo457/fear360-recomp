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

WINEDLLOVERRIDES="msvcp140,msvcp140_atomic_wait,vcruntime140,vcruntime140_1=n,b" %command% --resolution 720p --anisotropic_override 5 --game_data_root assets --user_data_root user --cache_root cache --nx_renderer native --fear_texture_pack texture_pack --fear_collection ../collection.toml

  4. Rename the entry (e.g. "F.E.A.R. Collection") and go back to Game Mode.

It starts at the collection menu; pick a game. Switch Game in either game's main menu starts the
other one. Settings are in each game's PC Settings menu (render scale is set to 1x here for the
Deck's 1280x800 screen).

Performance testing
  - The Deck's overlay: Quick Access (...) > Performance > Performance Overlay Level 3 or 4.
  - The frame cap follows the display: 60 fps on the LCD Deck, 90 on the OLED (the cap is 120).
    For a fixed lower rate, set the Deck's Framerate Limit, or PC Settings > Frame rate.
  - The first run translates shaders as they appear (brief hitches the first time an effect is
    seen); each later run starts with them pre-built, so test the second run for smoothness.
  - Without the PC textures (lighter on memory): delete " --fear_texture_pack texture_pack" from
    the launch options.
  - Logs: fear/logs/fear_NNN.log and fearfiles/logs/fearfiles_NNN.log (one per run). Every 10 s
    they give frame pacing, renderer CPU/GPU time per frame and slow frames. Copy them back to
    the PC for a read.

If it doesn't start
  - Make sure START IN is the fear folder (the paths in the launch options are relative to it).
  - Try another Proton version.
  - Send the newest log from fear/logs/.
