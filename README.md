# fear360-recomp

An unofficial static recompilation of the Xbox 360 releases of **F.E.A.R.** and **F.E.A.R. Files** for Windows and Linux, with native performance, an unlocked frame rate, and controller-first play.

**Status:** first Windows build with ReXGlue (2026-10-21): fullscreen 4K, widescreen and 16x anisotropic filtering. See [docs/PLAN.md](docs/PLAN.md).

The goal is a full PC version in the spirit of [OpenCE](https://github.com/OpenCommunityEdition/OpenCE): point it at your own game files, launch, and get real options. Windows first, then Linux.

This repository contains no game data. You need your own legally dumped discs; see [docs/RIPPING.md](docs/RIPPING.md).

## Build and run (Windows)
```
pwsh -NoProfile -File tools/build.ps1
pwsh -NoProfile -File tools/launch.ps1                 # fullscreen, 4K internal, 16x AF
pwsh -NoProfile -File tools/launch.ps1 -Scale 2 -Windowed
```
Put your own `default.xex` (title `565507D9`) and `Game/` folder in `port/assets/`.

## Credits
- [fear-nx](https://github.com/infriker/fear-nx) found the function entry points that F.E.A.R. needs under ReXGlue.
- [ReXGlue](https://github.com/rexglue/rexglue-sdk) and the [Xenia](https://github.com/xenia-project/xenia) contributors.
