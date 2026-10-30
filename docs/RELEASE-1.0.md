# F.E.A.R. 360 Recompiled 1.0 (Linux and Steam Deck, source release)

The Xbox 360 version of **F.E.A.R.**, statically recompiled to run **natively on Linux and the
Steam Deck**: no Proton, no Wine, Vulkan straight on the GPU driver, up to 120 fps, PC settings,
and the console's own game code.

**This release is source only.** It contains no game code, no game data and no executables. You
build it from your own Xbox 360 disc of F.E.A.R.: [SETUP.md](SETUP.md#linux-and-steam-deck) walks
through it.

**1.0 is F.E.A.R. alone, on Linux and the Steam Deck.** *F.E.A.R. Files* (Extraction Point +
Perseus Mandate) is listed in the menu as coming later, and the Windows build comes with 1.5. Both
still build from this source, but they aren't tested or supported in 1.0.

**1.0.1** fixes building from a `git clone`: since the v1.0.0 tag, the SDK took this repository's
"1.0" for its own version and stopped the CMake configure, on Windows and in the Steam Deck
container (SDK patch 0028; a source zip was not affected). If you cloned 1.0, pull and build again;
on Windows, run `tools/setup-sdk.ps1 -Force` first. The game itself is unchanged.

## What's in 1.0

**Native on the Steam Deck.** `tools/build-deck.sh` builds the game in an Ubuntu 22.04 container,
so it runs on SteamOS; `make_package.py --linux` packs it with your game data, and `fear-linux.sh`
goes into Steam as a non-Steam game with no compatibility tool. The same package runs on an
x86-64 Linux PC with a Vulkan 1.3 GPU.

**Performance on the Deck.** With the 360's settings (field of view 70, no bloom, no MSAA) and the
120 fps limit, over about 80 minutes of play on the release candidate (cc266c5, the same game code:
1.0 changes only the collection menu and the package), in Interval 03's water fights, Intervals 04
and 05 and Interval 11: a median frame of 8.35 ms (120 fps), 97% of frames at 90 fps or more, 99.8%
at 60 fps or more. What got it there:
- Vertex data the game rewrites every draw (particles) is copied to the byte, so its uploads no
  longer stall the GPU at every draw: 300-700 upload batches a frame became 1-6 in the water fights.
- Guest memory commits no longer read `/proc/self/maps`: the game thread's spikes while the game
  decompresses data are gone.
- The water's screen copies copy only what the draws since touched.
- Vertex ranges from the index bounds, ThinLTO over the game and the SDK, and optional
  profile-guided optimization from your own play ([SETUP.md](SETUP.md#linux-and-steam-deck)).

**Enemies that fight back.** The 360 version made Normal difficulty easier than the PC's. Now
every difficulty uses Very Hard's tactics by default: enemies aim well, push, dodge and throw
grenades. How hard they hit still follows the difficulty you pick. PC Settings → Enemy AI also
offers the PC's difficulties or the 360's originals (#3).

**High frame rates.** The console code assumes 30 fps in places; fixed so far:
- Enemies update every frame above 100 fps, so they react as quickly as on the console (#3).
- EchoPatch's fixes for sliding on slopes, the jump kick, speed on landing and out of water, water
  waves and splashes, and the slow-mo meter (#5, #6, #8, #9, #11-#13).
- Model animations advance by exactly the time that passed, and bullet tracers no longer stick in
  mid-air (#27).

**PC Settings** in the main menu: frame rate limit, anti-aliasing, texture filtering and
sharpness, field of view (#1), aim assist, right-stick feel (the 360's turn ramp off by default),
enemy AI, blood marks, EchoPatch's options, the interval select, animation timing and bloom (off
by default, as on PC: #38). *Quit to Desktop* is there too.

**Shaders built before play.** Each start builds every shader and pipeline earlier runs have seen,
on all CPU cores, before the main menu ("PREPARING SHADERS" if it takes a moment), so only the
very first run builds them as they appear (#53).

**The menus.** A collection menu in the game's own style (#43, #97), and PC Settings and Quit to
Desktop in their own column of the main menu, with help lines like the game's (#80).

## Requirements

A Steam Deck, or a Linux x86-64 PC with a Vulkan 1.3 GPU and a CPU with AVX2 and FMA. Your own
F.E.A.R. disc (Xbox 360). Building needs podman (or Ubuntu 22.04+ with clang 20), CMake, Ninja,
Git and Python 3: see [SETUP.md](SETUP.md#linux-and-steam-deck).

## Known issues

- Interval 03's biggest water fight can dip into the 50s for a second or two (#103).
- Docked at 120 Hz: the TV dropped out twice over the dock at 120 Hz, a DisplayPort link failure in
  the kernel log (#89). 60 Hz works.
- Occasional hitches in the heaviest fights (#82).
- High frame rates: bodies can be flung hard when bullet time ends (#62, #4); the walk animation
  can drop to idle (#10).
- The game draws at the 360's 1280×720, scaled to the screen; on-screen text is the 360's 720p
  bitmap fonts (#42).
- On the Steam Deck the controller can drop for about a second at story autosaves (#36).
- PC-parity fixes from EchoPatch are being worked through: #2 tracks them.

Report problems on GitHub with the newest files from `fear/logs` (the game's log and
`system-<time>.txt`).

## Credits

- [ReXGlue](https://github.com/rexglue/rexglue-sdk): the static recompilation SDK (BSD-3-Clause,
  derived in part from [Xenia](https://github.com/xenia-project/xenia)).
- [pgr4-native](https://github.com/shaunsingh/pgr4-native): the renderer (MIT), by way of
  GRAW 360 Recompiled.
- [fear-nx](https://github.com/infriker/fear-nx): the GPU ring, the native Vulkan Xenos renderer
  the game used up to 0.1.1, and the SDK patch (GPL-3.0-or-later), adapted from Project8Recomp
  and nfsmw-nx.
- [EchoPatch](https://github.com/Wemino/EchoPatch): the PC version's fixes, the standard this
  project measures itself against.

F.E.A.R. is a trademark of Warner Bros. Entertainment / Monolith Productions. This project is not
affiliated with or endorsed by them; see `NOTICE` for licensing.
