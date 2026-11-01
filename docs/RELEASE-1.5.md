# F.E.A.R. 360 Recompiled 1.5 (Windows, Linux and Steam Deck)

The Xbox 360 version of **F.E.A.R.**, statically recompiled to run natively on **Windows** and on
**Linux and the Steam Deck**.

**1.5 comes ready to play.** Download the zip for your system, unpack your own F.E.A.R. disc into
it and start the game: the zip's README.txt walks through it. The zips hold the game's program
only, **no game data**: you need your own Xbox 360 disc of F.E.A.R. Building from source still
works ([SETUP.md](SETUP.md)).

*F.E.A.R. Files* (Extraction Point + Perseus Mandate) comes in a later release.

## New in 1.5

**Windows.** The first Windows release: the Direct3D 12 renderer at your display's resolution, up
to 4K, at up to 120 fps (Recompilation Settings > Resolution).

**Keyboard and mouse.** Play with the keyboard and mouse, PC style, or with a controller: the
first start asks which (#115, #117, #118). Mouse look, the PC's default keys, separate left and
right Ctrl and Shift (#121), the PC's Strafe key, the mouse in every menu (#109, #110), and the
menus' button prompts show Enter and Esc. Recompilation Settings > Controls rebinds every key and
sets the mouse's sensitivity, smoothing and inversion (#59). Changing the input restarts the game
so its button prompts follow. On Linux the first start asks too, except on the Steam Deck (#123).

**PC textures and sharper text.** With your own copy of the PC game, a texture pack built on your
PC swaps in the PC version's textures, and redraws the game's text from its own typefaces at four
times the detail, so menus and the HUD stay crisp at 4K (#42; [TEXTURES.md](TEXTURES.md)). Both
renderers use it: Windows and, new in 1.5, Linux and the Deck.

**John Wick mode.** Recompilation Settings > John Wick mode: infinite ammo, dual pistols, faster
movement and a SlowMo meter that never runs out (#61).

**Recompilation Settings** (was PC Settings): two columns, so every option fits at 720p (#124),
an opaque panel that holds its size, Back saves your changes, field of view up to 100, and text
written the way the game's own options are.

## Requirements

- **Windows:** Windows 10 or 11 64-bit, a CPU with AVX2, a Direct3D 12 GPU.
- **Linux:** a Steam Deck, or an x86-64 Linux PC with a Vulkan 1.3 GPU and a CPU with AVX2 and FMA.
- Your own F.E.A.R. disc (Xbox 360) as a disc image, and Python 3 to unpack it.

## Known issues

- On a GTX 1650 laptop, holding 30 fps needs the Xbox 360's settings (no MSAA, 1280x720).
- Interval 03's biggest water fight can dip into the 50s for a second or two on the Deck (#103).
- Docked at 120 Hz, the Deck's TV output dropped out twice (#89). 60 Hz works.

The source of this release is the `v1.5.0` tag.
