# F.E.A.R. 360 Recompiled 1.0 (Windows, source release)

The Xbox 360 versions of **F.E.A.R.** and **F.E.A.R. Files** (Extraction Point + Perseus
Mandate), statically recompiled to run natively on Windows: high resolutions, high frame rates,
PC settings, and the console's own game code.

**This release is source only.** It contains no game code, no game data and no executables.
You build it on your own PC from your own Xbox 360 discs: [SETUP.md](SETUP.md) walks through it.

## What's in 1.0

**Both games in one collection.** A menu picks F.E.A.R. or F.E.A.R. Files before either loads;
*Switch Game* in each game's main menu returns to it.

**Graphics**
- A native Direct3D 12 renderer (ported from GRAW 360 Recompiled, after pgr4-native) that draws
  the game's own Direct3D calls once per draw, with the game's own 2×/4× MSAA (or off), up to 16×
  anisotropic filtering and sharper textures. Resolution scaling up to 3× (3840×2160 from the
  console's 1280×720) and the PC texture pack come back to it before 1.0.
- Bloom off by default, as on PC: the 360 version's screen bloom spread a grey haze near bright
  lights (#38). PC Settings can turn it back on.
- Optional: the PC version's higher-resolution textures, built from your own PC copy into a local
  pack ([TEXTURES.md](TEXTURES.md)).
- Blood and armour marks on enemies where your shots hit, at the PC's Maximum detail. The 360
  version starts with them switched off (#47).
- Sharper distant textures, like EchoPatch's mipmap bias on PC (#50); PC Settings has three
  levels.

**Enemies that fight back.** The 360 versions made Normal difficulty easier than the PC's. Now
every difficulty uses Very Hard's tactics by default: enemies aim well, push, dodge and throw
grenades. How hard they hit still follows the difficulty you pick. PC Settings → Enemy AI also
offers the PC's difficulties or the 360's originals (#3).

**High frame rates.** Up to 120 fps, paced to your display. The console code assumes 30 fps in
places; fixed so far:
- Enemies update every frame above 100 fps, so they react as quickly as on the console (#3).
- Model animations advance by exactly the time that passed (the engine dropped a fraction of a
  millisecond every frame).
- Bullet tracers no longer stick in mid-air (#27).

**PC Settings** in each game's main menu: resolution scale, window mode, frame rate limit,
anti-aliasing, texture filtering and sharpness, field of view (#1), aim assist, right-stick feel
(the 360's turn ramp off by default), enemy AI, blood marks, EchoPatch's options and high frame
rate fixes, the interval select, animation timing, PC textures and bloom. *Quit to
Desktop* is there too.

**Performance.** The recompiled code keeps PowerPC registers in CPU registers where it can (#34):
about 30% less CPU time on the game thread than before, measured on PC. Background threads that
only spun or polled under Proton were cut back for the Steam Deck (#40; not measured there yet).

**Shaders built before play.** Each start builds every shader and pipeline your PC has seen in
earlier runs, on all CPU cores, before the main menu, with a progress panel if it takes more than
a moment. A graphics driver update or a cleared cache no longer brings the stutter back; only the
very first run builds them as they appear. To skip that on another PC (a laptop, the Deck),
`tools/make_warmup.py` turns one PC's shader cache and your own game files into a warm-up list the
next build compiles in. The list is derived from the game, so it stays on your PC: it isn't part of
the source and must not be shared (#53).

**Steam Deck (experimental).** The Windows build runs under Proton; it held a median of 89 fps
on a Deck OLED in testing, with the heaviest scenes around 74. Packaging steps:
`tools/steamdeck/README-SteamDeck.txt`.

## Requirements

Windows 10 or 11 (64-bit), a CPU with AVX2 and FMA (Intel Haswell / AMD Excavator or newer), a
Direct3D 12 GPU, an Xbox controller, and your own discs. Building needs Visual Studio 2022's C++
workload, LLVM 20, CMake, Ninja, Git, PowerShell 7 and Python: see [SETUP.md](SETUP.md).

## Known issues

- GPU load jumps during Interval 2's closing transformation sequence (#41).
- On-screen text comes from the 360's 720p bitmap fonts and looks soft at high resolution
  scales (#42).
- The collection menu is a plain panel without the games' menu effects and sounds (#43). If the
  controller doesn't respond there, click the game window once.
- The first-person weapon isn't corrected for wide fields of view yet.
- Whether the 360-only Holiday bonus mission unlocks normally isn't checked yet (#39).
- The Sharp enemy AI is new, and its tuning across difficulties is still being play-tested (#3).
- On Windows the games read controllers through XInput (Xbox pads, or anything Steam Input
  presents as one): SDL found no controller in the game on one laptop (#54). `input_backend =
  "sdl"` in `user/settings.toml` brings SDL back. The Steam Deck keeps SDL.
- On the Steam Deck the controller can drop for about a second at story autosaves (#36).
- PC-parity fixes from EchoPatch are being worked through: #2 tracks them.

Report problems on GitHub with the crash report or log from `port-nx/logs` or `port-files/logs`.

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk): the static recompilation SDK (BSD-3-Clause,
  derived in part from [Xenia](https://github.com/xenia-project/xenia)).
- [pgr4-native](https://github.com/shaunsingh/pgr4-native): the Direct3D 12 renderer (MIT), by way of
  GRAW 360 Recompiled.
- [fear-nx](https://github.com/infriker/fear-nx): the GPU ring, the native Vulkan Xenos renderer
  the game used up to 0.1.1, and the SDK patch (GPL-3.0-or-later), adapted from Project8Recomp
  and nfsmw-nx.
- [EchoPatch](https://github.com/Wemino/EchoPatch): the PC version's fixes, the standard this
  project measures itself against.

F.E.A.R. is a trademark of Warner Bros. Entertainment / Monolith Productions. This project is not
affiliated with or endorsed by them; see `NOTICE` for licensing.
