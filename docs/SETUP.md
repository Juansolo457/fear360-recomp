# Build and play from your own disc

**To just play:** 1.5's release has ready-built zips for Windows and for Linux and the Steam
Deck; unpack your disc into one and play (its README.txt says how). This page is for building the
game yourself. *F.E.A.R. Files* (Extraction Point + Perseus Mandate) comes in a later release: its
steps below still build, but it isn't tested or supported yet.

No download contains game data: it comes from your own disc of **F.E.A.R.** (and, once it's
supported, of **F.E.A.R. Files**). Building translates the game's code from that disc on your
own PC.

The first build takes 20–40 minutes. After that, rebuilds take a minute or two.

## 1. What you need

**Hardware**
- A 64-bit CPU with AVX2 and FMA: Intel Haswell (2013) or AMD Excavator (2015) and newer. The
  game stops with a message on older CPUs.
- A Direct3D 12 GPU with a current driver (NVIDIA, AMD or Intel).
- About 15 GB free disk space for both games (toolchain, SDK, two builds, two extracted discs).

**Software**
- Windows 10 or 11, 64-bit.
- [Visual Studio 2022](https://visualstudio.microsoft.com/downloads/) (Community or Build Tools)
  with the **Desktop development with C++** workload (MSVC v143 and a Windows 11 SDK). The build
  uses its libraries and command environment; the compiler itself is LLVM.
- [Git for Windows](https://git-scm.com/download/win).
- [PowerShell 7](https://learn.microsoft.com/powershell/scripting/install/installing-powershell-on-windows)
  (`pwsh`).
- [Python](https://www.python.org/downloads/) 3.10 or newer, on PATH.

**Toolchain in `.tools/`** (in the repository folder; download each from its official release
page and unpack it exactly here):

| Tool | Version | Download | Ends up as |
|---|---|---|---|
| LLVM | 20.1.8 | [LLVM-20.1.8-win64.exe](https://github.com/llvm/llvm-project/releases/tag/llvmorg-20.1.8) | `.tools/llvm/bin/clang++.exe` |
| CMake | 3.31.8 | [cmake-3.31.8-windows-x86_64.zip](https://github.com/Kitware/CMake/releases/tag/v3.31.8) | `.tools/cmake/cmake-3.31.8-windows-x86_64/bin/cmake.exe` |
| Ninja | 1.13.1 | [ninja-win.zip](https://github.com/ninja-build/ninja/releases/tag/v1.13.1) | `.tools/ninja/ninja.exe` |

The LLVM installer can install into `.tools\llvm` directly: run it and pick that folder, or from
PowerShell in the repository folder:
`Start-Process -Wait LLVM-20.1.8-win64.exe -ArgumentList '/S', "/D=$PWD\.tools\llvm"`.

## 2. Get the code and the SDK

```
git clone https://github.com/Juansolo457/fear360-recomp
cd fear360-recomp
pwsh -NoProfile -File tools/setup-sdk.ps1
```

`setup-sdk.ps1` clones the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) at v0.10.0,
applies this project's patches (`patches/rexglue-sdk-nx/`: fear-nx's GPU support, then ours), checks the result against the exact source the release was built from, and fetches
the SDK's own dependencies. It puts everything in `third_party/rexglue-sdk-nx`.

## 3. Dump your discs

You need a disc image (ISO) of each game. A normal PC drive can't read Xbox 360 game discs; see
[RIPPING.md](RIPPING.md) for the ways to dump your own. Then unpack each image into its game's
folder:

```
python -I tools/xdvdfs.py "<your F.E.A.R. ISO>" extract port-nx/assets
python -I tools/xdvdfs.py "<your F.E.A.R. Files ISO>" extract port-files/assets
```

Each folder should then hold `default.xex` and a `Game` folder. The build checks the executable
and accepts only these (the versions the translation was made for):

| Game | Title ID | `default.xex` SHA-256 |
|---|---|---|
| F.E.A.R. | `565507D9` | `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792` |
| F.E.A.R. Files | `565507E8` | `a51deed1dc0842af32a3a2b811674552f4ae5b6339ce8b9abe0eabe14ba6c7eb` |

Nothing from the disc ever goes into the repository: these folders are ignored by git.

## 4. Build

```
pwsh -NoProfile -File tools/build-nx.ps1
pwsh -NoProfile -File tools/build-nx.ps1 -Project port-files
```

The first command builds the SDK, translates F.E.A.R.'s code into C++ (about 200 MB of it) and
compiles the game: `port-nx/out/build/win-amd64-relwithdebinfo/fear.exe`. The second builds
F.E.A.R. Files: `port-files/out/build/win-amd64-relwithdebinfo/fearfiles.exe`. These programs
contain code translated from your disc: keep them to yourself.

## 5. Play

```
pwsh -NoProfile -File tools/launch.ps1 -Collection
```

This opens the collection menu (add `-WithFiles` to make *F.E.A.R. Files* playable there; without it, Files is listed as coming later): pick **F.E.A.R.** or **F.E.A.R. Files**; *Switch Game* in either
game's main menu comes back to it. Play with an Xbox controller, as on the console; plug it in
before you start. If the controller doesn't react in the collection menu, click the game window
once so it has focus. On Windows the games read controllers through XInput, so other pads
(PlayStation, Switch) need Steam Input or another XInput wrapper, or the SDL setting under
*When something goes wrong*.

In each game's main menu, **Recompilation Settings** sets the frame rate limit, anti-aliasing, texture
filtering and sharpness, field of view, aim assist, right-stick feel, EchoPatch's options and
more. Saves and settings stay in each
game's `user` folder.

Useful launcher options: `-FrameRate <n>` (a frame rate limit), `-Windowed`. All of them are
described at the top of `tools/launch.ps1`. *Recompilation Settings → Resolution* draws the game
at 1280×720, 2560×1440 or 3840×2160 (*Match display*: the smallest at least as tall as your
screen).

**PC textures and sharper text.** If you also own the PC version, build a pack of its
higher-resolution textures and the redrawn fonts ([TEXTURES.md](TEXTURES.md)); both renderers use it.

**Steam Deck:** use the native Linux build below.

## Linux and Steam Deck

F.E.A.R. as a native Linux program on the renderer's Vulkan backend ([LINUX_NATIVE.md](LINUX_NATIVE.md)):
no Proton, no Wine. The Steam Deck is what 1.0 was tested on.

**Where to build:** a Linux x86-64 PC, or WSL2 (Ubuntu) on a Windows PC. In WSL, clone into
WSL's own file system (`~/`), not under `/mnt/c`: a Windows checkout's CRLF line endings break
the SDK's FFmpeg build. The disc dumps can stay on the Windows side; link them into each game's
`assets` folder.

**What you need:**
- For the Steam Deck: [podman](https://podman.io/). `tools/build-deck.sh` builds in an Ubuntu 22.04
  container (`tools/linux/deck.Containerfile`, made on first use), so the programs run on
  SteamOS's glibc, and brings its own clang 20.
- For your own Linux PC instead: clang 20 or newer with its lld, llvm-ar and llvm-ranlib (the
  builds use ThinLTO; `FEAR_LTO=OFF` builds without it), CMake 3.25+, Ninja, and the SDK's system
  libraries: `sudo apt install libgtk-3-dev libx11-xcb-dev libxss-dev libvulkan-dev
  libwayland-dev libwayland-bin wayland-protocols libxkbcommon-dev libdecor-0-dev libasound2-dev
  libpulse-dev libpipewire-0.3-dev` (on 22.04 also libstdc++ 13 from the toolchain PPA; the
  Containerfile has the whole setup). To play: a GPU with Vulkan 1.3 and a CPU with AVX2 and FMA.
- Either way: Git, Python 3, and your F.E.A.R. disc dumped as in step 3:
  `python3 -I tools/xdvdfs.py "<your F.E.A.R. ISO>" extract port-nx/assets`.

**Build:**

```
git clone https://github.com/Juansolo457/fear360-recomp
cd fear360-recomp
```

then, for the Steam Deck (the container sets up its own patched SDK, as `tools/setup-sdk.sh`
does):

```
tools/build-deck.sh
python3 -I tools/steamdeck/make_package.py --linux
```

or, for your own Linux PC, `tools/setup-sdk.sh`, then `tools/build-linux.sh`. The first build
compiles the SDK and the game's translated code (about 200 MB of C++), so it takes a long while;
later ones are quick.

**Play on the Steam Deck:** `make_package.py --linux` writes
`private/steamdeck/FEAR360-SteamDeck-linux.zip` (about 4 GB): F.E.A.R.'s program, your game data
and the Deck's starting settings. Copy it to the Deck, then follow
`tools/steamdeck/README-SteamDeck-Linux.txt` (it's in the zip): extract it, add `fear-linux.sh` to
Steam as a non-Steam game, and leave Proton off. The zip contains your game data and a program
made from your disc: keep it to yourself. Later updates: `--linux --programs` makes a small zip of
just the program to extract over it. The menu lists *F.E.A.R. Files* as coming later.

*F.E.A.R. Files* for development (not part of 1.0): `tools/build-deck.sh --project port-files`
with its disc in `port-files/assets`, then `make_package.py --linux --files`; `FEAR_FILES=1
./fear-linux.sh` lists it in the menu.

**Play on your own Linux PC:** the same package runs on an x86-64 Linux with a CPU with AVX2 and
FMA (x86-64-v3), glibc 2.35 or newer, libstdc++ 13 or newer (`GLIBCXX_3.4.32`; SteamOS and
current distributions have it, Ubuntu 22.04 needs the toolchain PPA's) and a Vulkan 1.3 GPU:
extract it anywhere and run `./fear-linux.sh` in its `FEAR360-SteamDeck`
folder (`./fear-linux.sh --no-fullscreen` for a window). `tools/build-linux.sh`'s builds are for
development: each game's program is in `<project>/out/build/linux-amd64-relwithdebinfo/`, run
from the game's folder with the flags `fear-linux.sh` passes (`--game_data_root`,
`--user_data_root`, `--cache_root`).

Optional, faster on the Deck: profile-guided optimization (PGO) from your own play. The profile
comes from running the game's code, so it isn't in the repository; each player makes their own:

1. Build the training version: `FEAR_PGO=generate tools/build-deck.sh`, then
   `python3 -I tools/steamdeck/make_package.py --linux --pgogen --programs`
   (`private/steamdeck/FEAR360-SteamDeck-linux-pgogen.zip`), and extract that zip over your
   install on the Deck.
2. Play it for 5-10 minutes, in the heavy fights (it runs slower while it counts). The game
   writes `pgo-raw/*.profraw` beside its program.
3. `tools/pgo-merge.sh --for deck --project port-nx <the pgo-raw folder>` (`--for host` for
   builds made with `tools/build-linux.sh` on your own Linux PC).
4. Build as usual: the builds use `pgo/<project>/default.profdata` when it's there. Then
   `make_package.py --linux --programs` and extract that update zip over the install, in place of
   the training build.

## When something goes wrong

- **"default.xex is not the supported executable":** the disc is a different version from the
  one above. Only those executables are supported.
- **"Toolchain missing" or "Patched SDK missing":** check step 1's `.tools` layout, or run
  `tools/setup-sdk.ps1` again (with `-Force` to start over).
- **A controller that isn't an Xbox one:** add `input_backend = "sdl"` to the game's
  `user/settings.toml`. SDL also reads PlayStation and Switch pads, but on one Windows 11 laptop
  it found no usable pad in the game (#54), which is why XInput is the Windows default.
- **Hitches the first time you play, or a pause before the main menu ("PREPARING SHADERS"):**
  the first run builds each shader and pipeline as it first appears; they're kept in the game's
  `cache` folder, and every later start builds them all before the main menu, which is quick once
  the driver has them (#53). To take one PC's shaders to another, `tools/make_warmup.py` (its
  header has the commands) writes a warm-up list for the next build. That list is derived from
  the game: keep it local, never commit or share it.
- **A crash:** the game writes `crash-<time>.txt` and `.dmp` to `port-nx/logs` (or
  `port-files/logs`), next to each run's log. Please attach the text file to an issue on GitHub.
