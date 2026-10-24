# Build and play from your own disc (Windows)

This project ships **source only**. There is no download that contains the game: the game's
code is translated from your copy of the Xbox 360 disc on your own PC, and the game data comes
from the same disc. You need your own discs of **F.E.A.R.** and, for the second game,
**F.E.A.R. Files** (Extraction Point + Perseus Mandate).

The first build takes 20–40 minutes. After that, rebuilds take a minute or two.

## 1. What you need

**Hardware**
- A 64-bit CPU with AVX2 and FMA: Intel Haswell (2013) or AMD Excavator (2015) and newer. The
  game stops with a message on older CPUs.
- A GPU with a current Vulkan driver (NVIDIA, AMD or Intel).
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
applies this project's patches (`patches/rexglue-sdk-nx/`: fear-nx's Vulkan renderer support,
then ours), checks the result against the exact source the release was built from, and fetches
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

This opens the collection menu: pick **F.E.A.R.** or **F.E.A.R. Files**; *Switch Game* in either
game's main menu comes back to it. Play with an Xbox controller, as on the console; plug it in
before you start. If the controller doesn't react in the collection menu, click the game window
once so it has focus. On Windows the games read controllers through XInput, so other pads
(PlayStation, Switch) need Steam Input or another XInput wrapper, or the SDL setting under
*When something goes wrong*.

In each game's main menu, **PC Settings** sets the resolution scale, frame rate limit, texture
filtering, field of view, aim assist, right-stick feel and more. Saves and settings stay in each
game's `user` folder.

Useful launcher options: `-Scale 1..3` (internal resolution; 3 = 3840×2160), `-FrameRate <n>`
(a frame rate limit), `-Windowed`. All of them are described at the top of `tools/launch.ps1`.

**Optional: PC textures.** If you also own the PC version, you can build a pack of its
higher-resolution textures once and use them in both games: [TEXTURES.md](TEXTURES.md).

**Steam Deck:** experimental. Build on a Windows PC as above, then package and copy it over:
`tools/steamdeck/README-SteamDeck.txt`.

## When something goes wrong

- **"default.xex is not the supported executable":** the disc is a different version from the
  one above. Only those executables are supported.
- **"Toolchain missing" or "Patched SDK missing":** check step 1's `.tools` layout, or run
  `tools/setup-sdk.ps1` again (with `-Force` to start over).
- **A controller that isn't an Xbox one:** add `input_backend = "sdl"` to the game's
  `user/settings.toml`. SDL also reads PlayStation and Switch pads, but on one Windows 11 laptop
  it found no usable pad in the game (#54), which is why XInput is the Windows default.
- **A pause before the main menu on the first start ("PREPARING SHADERS"):** the game builds its
  shaders and pipelines for your graphics card and driver, once, so play doesn't hitch. They're
  kept in the game's `cache` folder; later starts skip it (#53).
- **A crash:** the game writes `crash-<time>.txt` and `.dmp` to `port-nx/logs` (or
  `port-files/logs`), next to each run's log. Please attach the text file to an issue on GitHub.
