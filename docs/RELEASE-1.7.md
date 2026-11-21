# F.E.A.R. 360 Recompiled 1.7 (Windows, Linux and Steam Deck)

The Xbox 360 version of **F.E.A.R.**, statically recompiled to run natively on **Windows** and on
**Linux and the Steam Deck**.

Download the zip for your system, unpack your own F.E.A.R. disc into it and start the game: the
zip's README.txt walks through it. The zips hold the game's program only, **no game data**: you need
your own Xbox 360 disc of F.E.A.R. Building from source still works ([SETUP.md](SETUP.md)).

## New in 1.7

**F.E.A.R. Files on Linux and the Steam Deck.** The Linux zip now holds the program for *F.E.A.R.
Files* (Extraction Point + Perseus Mandate) beside F.E.A.R.'s. Unpack your own Xbox 360 disc of
F.E.A.R. Files into `fearfiles/assets` and the menu the game starts at lists it as a game to play;
*Switch Game* in either main menu goes to the other. It's optional: without that disc the menu
lists it as coming later, and F.E.A.R. plays as before.

It is still being play-tested: not every level has been played to the end (#57), and its own
effects haven't all been checked (#51). The Windows zip is F.E.A.R. alone for now.

**Weapon cycling.** The fix that makes next and previous weapon skip empty slots changed what the
game was told about a weapon's slot for the whole of a cycle. A weapon removed during one could
have had the slot beside it cleared instead (#132; found by reading the code, not seen in play).
Only the cycle's own lookup is changed now.

**A check that the game is the supported one.** Every fix in this build is written for one
version of the game's executable. At startup the game now compares the code at each of them with
what the fix was written for. On another version of the disc it says so in the log instead of
misbehaving without a word, and beside the build's version in the corner of the screen while that
is showing (the first 30 seconds after the program starts, unless set to always). A few fixes also
check what they find as the game runs, and stand aside when it isn't what they expect.

## Requirements

- **Windows:** Windows 10 or 11 64-bit, an x86-64-v3 CPU (AVX2, FMA and BMI), a Direct3D 12 GPU.
- **Linux:** a Steam Deck, or an x86-64 Linux PC with a Vulkan 1.3 GPU, an x86-64-v3 CPU (AVX2, FMA and BMI),
  glibc 2.35 or newer and a libstdc++ with `GLIBCXX_3.4.32` (GCC 13's; Ubuntu 22.04 needs a newer
  libstdc++6, SteamOS has it).
- Your own F.E.A.R. disc (Xbox 360) as a disc image, and Python 3 to unpack it.
- For F.E.A.R. Files (Linux): your own F.E.A.R. Files disc (Xbox 360) as a disc image.

## Known issues

- F.E.A.R. Files: not every level has been played to the end (#57).
- After coming down a ladder and taking damage, a weapon switch once left no weapon selected.
  Seen once on the Deck and not reproduced; switching again selects one.
- Holiday (Bonus Mission): after a death, the game once stayed on a near-empty screen instead of
  reloading. Seen once; it didn't happen again in three deaths on Extreme, slow-mo included.
- No save anywhere: the game saves at its checkpoints only, as the Xbox 360 version did.
- The right stick can stop responding when the game saves a checkpoint.
- On a GTX 1650 laptop, holding 30 fps needs the Xbox 360's settings (no MSAA, 1280x720).
- Interval 03's biggest water fight can dip into the 50s for a second or two on the Deck (#103).
- Docked at 120 Hz, the Deck's TV output dropped out twice (#89). 60 Hz works.

The source of this release is the `v1.7.0` tag.
