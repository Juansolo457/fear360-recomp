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
- fear-nx's native Vulkan renderer for the Xbox 360 GPU, with integer resolution scaling up to 3×
  (3840×2160 from the console's 1280×720), the game's own 2×/4× MSAA and up to 16× anisotropic
  filtering.
- Bloom off by default, as on PC: the 360 version's screen bloom spread a grey haze near bright
  lights (#38). PC Settings can turn it back on.
- Optional: the PC version's higher-resolution textures, built from your own PC copy into a local
  pack ([TEXTURES.md](TEXTURES.md)).

**High frame rates.** Up to 120 fps, paced to your display. The console code assumes 30 fps in
places; fixed so far:
- Enemies update every frame above 100 fps, so they react as quickly as on the console (#3).
- Model animations advance by exactly the time that passed (the engine dropped a fraction of a
  millisecond every frame).
- Bullet tracers no longer stick in mid-air (#27).

**PC Settings** in each game's main menu: resolution scale, window mode, frame rate limit,
texture filtering, field of view (#1), aim assist, right-stick feel (the 360's turn ramp off by
default), animation timing, PC textures and bloom. *Quit to Desktop* is there too.

**Performance.** The recompiled code keeps PowerPC registers in CPU registers where it can (#34):
about 30% less CPU time on the game thread than before, measured on PC. Background threads that
only spun or polled under Proton were cut back for the Steam Deck (#40; not measured there yet).

**No first-run stutter.** The first start of each game on a new PC or graphics driver builds the
shaders and pipelines seen in play, on all CPU cores, before the main menu, with a progress panel
if it takes more than a moment. The shaders come from your own disc's files. Before, each one was
built the first time something drew with it, so the first load and every new area hitched (#53).

**Steam Deck (experimental).** The Windows build runs under Proton; it held a median of 89 fps
on a Deck OLED in testing, with the heaviest scenes around 74. Packaging steps:
`tools/steamdeck/README-SteamDeck.txt`.

## Requirements

Windows 10 or 11 (64-bit), a CPU with AVX2 and FMA (Intel Haswell / AMD Excavator or newer), a
Vulkan GPU, an Xbox controller, and your own discs. Building needs Visual Studio 2022's C++
workload, LLVM 20, CMake, Ninja, Git, PowerShell 7 and Python: see [SETUP.md](SETUP.md).

## Known issues

- GPU load jumps during Interval 2's closing transformation sequence (#41).
- On-screen text comes from the 360's 720p bitmap fonts and looks soft at high resolution
  scales (#42).
- The collection menu is a plain panel without the games' menu effects and sounds (#43). If the
  controller doesn't respond there, click the game window once.
- The first-person weapon isn't corrected for wide fields of view yet.
- Whether the 360-only Holiday bonus mission unlocks normally isn't checked yet (#39).
- The AI can still feel slower than in the PC version at high frame rates; whether that's the
  360's own tuning is being checked (#3).
- On Windows the games read controllers through XInput (Xbox pads, or anything Steam Input
  presents as one): SDL found no controller in the game on one laptop (#54). `input_backend =
  "sdl"` in `user/settings.toml` brings SDL back. The Steam Deck keeps SDL.
- On the Steam Deck the controller can drop for about a second at story autosaves (#36).
- PC-parity fixes from EchoPatch are being worked through: #2 tracks them.

Report problems on GitHub with the crash report or log from `port-nx/logs` or `port-files/logs`.

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk): the static recompilation SDK (BSD-3-Clause,
  derived in part from [Xenia](https://github.com/xenia-project/xenia)).
- [fear-nx](https://github.com/infriker/fear-nx): the native Vulkan Xenos renderer and SDK patch
  (GPL-3.0-or-later), adapted from Project8Recomp and nfsmw-nx.
- [EchoPatch](https://github.com/Wemino/EchoPatch): the PC version's fixes, the standard this
  project measures itself against.

F.E.A.R. is a trademark of Warner Bros. Entertainment / Monolith Productions. This project is not
affiliated with or endorsed by them; see `NOTICE` for licensing.
