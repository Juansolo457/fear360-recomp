# F.E.A.R. 360 Recompiled 1.5 (Windows, Linux and Steam Deck)

The Xbox 360 version of **F.E.A.R.**, statically recompiled to run natively on **Windows** and on
**Linux and the Steam Deck**.

**1.5 comes ready to play.** Download the zip for your system, unpack your own F.E.A.R. disc into
it and start the game: the zip's README.txt walks through it. The zips hold the game's program
only, **no game data**: you need your own Xbox 360 disc of F.E.A.R. Building from source still
works ([SETUP.md](SETUP.md)).

*F.E.A.R. Files* (Extraction Point + Perseus Mandate) comes in a later release.

## New in 1.5

**Windows.** The first Windows release: the Direct3D 12 renderer at 1280x720, 2560x1440 or
3840x2160 (Recompilation Settings > Resolution; *Match display* picks the smallest at least as tall
as your screen, so a 1080p screen gets 2560x1440, scaled down), at up to 120 fps. Linux and the
Steam Deck draw at 1280x720.

**Keyboard and mouse.** Play with the keyboard and mouse, PC style, or with a controller: the
first start asks which (#115, #117, #118). Mouse look, the PC's default keys, separate left and
right Ctrl and Shift (#121), the PC's Strafe key, the mouse in every menu (#109, #110), and the
menus' button prompts show Enter and Esc. Recompilation Settings > Controls rebinds every key and
sets the mouse's sensitivity, smoothing and inversion (#59). Changing the input restarts the game
so its button prompts follow. On Linux the first start asks too, except on the Steam Deck (#123).

**PC textures and sharper text.** With your own copy of the PC game, a texture pack built on your
PC swaps in the PC version's textures, and redraws the game's text from its own typefaces at four
times the detail, so menus and the HUD stay crisp at 4K (#42). Both renderers use it: Windows and,
new in 1.5, Linux and the Deck. The release zips carry the pack builder: the steps are in
[TEXTURES.md](TEXTURES.md#from-a-release-zip).

**John Wick mode.** Recompilation Settings > John Wick mode: infinite ammo, dual pistols, faster
movement and a SlowMo meter that never runs out (#61).

**Recompilation Settings** (was PC Settings): two columns, so every option fits at 720p (#124),
an opaque panel that holds its size, Back saves your changes, field of view up to 100, and text
written the way the game's own options are.

## Requirements

- **Windows:** Windows 10 or 11 64-bit, a CPU with AVX2, a Direct3D 12 GPU.
- **Linux:** a Steam Deck, or an x86-64 Linux PC with a Vulkan 1.3 GPU, a CPU with AVX2 and FMA,
  glibc 2.35 or newer and a libstdc++ with `GLIBCXX_3.4.32` (GCC 13's; Ubuntu 22.04 needs a newer
  libstdc++6, SteamOS has it).
- Your own F.E.A.R. disc (Xbox 360) as a disc image, and Python 3 to unpack it.

## Known issues

- Holiday (Bonus Mission): after a death, the game once stayed on a near-empty screen instead of
  reloading. Seen once; it didn't happen again in three deaths on Extreme, slow-mo included.
- No save anywhere: the game saves at its checkpoints only, as the Xbox 360 version did.
- The right stick can stop responding when the game saves a checkpoint.
- On a GTX 1650 laptop, holding 30 fps needs the Xbox 360's settings (no MSAA, 1280x720).
- Interval 03's biggest water fight can dip into the 50s for a second or two on the Deck (#103).
- Docked at 120 Hz, the Deck's TV output dropped out twice (#89). 60 Hz works.

The source of this release is the `v1.5.0` tag.
