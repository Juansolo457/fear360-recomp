# F.E.A.R. 360 Recompiled 1.6 (Windows, Linux and Steam Deck)

The Xbox 360 version of **F.E.A.R.**, statically recompiled to run natively on **Windows** and on
**Linux and the Steam Deck**.

Download the zip for your system, unpack your own F.E.A.R. disc into it and start the game: the
zip's README.txt walks through it. The zips hold the game's program only, **no game data**: you need
your own Xbox 360 disc of F.E.A.R. Building from source still works ([SETUP.md](SETUP.md)).

*F.E.A.R. Files* (Extraction Point + Perseus Mandate) comes in a later release.

## New in 1.6

**Automatic shotgun.** Recompilation Settings > Automatic shotgun swaps the VK-12 for an automatic
shotgun left unused on the Xbox 360 disc, wherever a VK-12 is found. It's off by default. This is
the disc's own weapon, not yet the full PS3 Watson SAS-12 (#127): it has no PS3 sounds or level
placements.

With your own PS3 copy of F.E.A.R., the `fear_ps3_game` setting in `user/settings.toml` can point at
its `PS3_GAME/USRDIR/game` folder, and the shotgun then uses the PS3 version's first-person
animations and models. The game writes a patched copy of its catalogue, about 428 MB, to its user
folder. Nothing from the PS3 disc is in the zips.

**Sights at a wide field of view.** Above a field of view of 70, the game moves the first-person
camera forward so the gun keeps its place on screen. That push now fades out as you aim, for every
gun in F.E.A.R.

**Easier setup.**

- `xdvdfs.py` refuses an unpack that would land in a folder inside itself, such as
  `fear\assets\fear\assets`. It also says where the game went, and warns if `default.xex` isn't from
  a supported disc.
- The README warns that WinRAR, 7-Zip and other archivers can't unpack an Xbox 360 disc image: they
  only show a small video section, not the game.
- If you start the game with that video section in `fear\assets`, the launcher now says what went
  wrong, and the missing-files message says where the files go.

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

The source of this release is the `v1.6.0` tag.
