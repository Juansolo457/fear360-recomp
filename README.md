# F.E.A.R. 360 Recompiled

The Xbox 360 F.E.A.R., running natively on PC. 4K, 120 fps, a controller in your hands.

> Unofficial. **No game data included.** Bring your own Xbox 360 disc.

## Why

The 360 version came out about a year after the PC original and is the more complete one: **Instant Action** mode, a dual-wieldable machine pistol and an extra bonus level that the PC never got. It has been stuck on the console ever since, at 720p and 30 fps. The plan is to free it, then add *F.E.A.R. Files* (Extraction Point + Perseus Mandate, 360) so the whole F.E.A.R. 1 package lives in one place.

## What it is

A **static recompilation**: the game's PowerPC code is translated to native x86-64 C++ ahead of time ([ReXGlue](https://github.com/rexglue/rexglue-sdk)), and a game-specific runtime stands in for the Xbox. It is not an emulator and not a decompilation.

- **CPU:** the 360's `default.xex`, recompiled. All game logic, AI and physics are the console's own.
- **GPU:** [fear-nx](https://github.com/infriker/fear-nx)'s Vulkan Xenos renderer, which fixed the menu and video corruption the stock ReXGlue renderer has with this game, plus our resolution scaling.
- **Your data:** the game files come from your own disc; nothing proprietary is in this repo.

## Status

| | |
|---|---|
| ✅ Boots, menus, intro videos, campaign, Instant Action | `port-nx` with `--nx_renderer native` |
| ✅ 4K: integer internal resolution scaling (3× = 3840×2160) | our patch to fear-nx's renderer |
| ✅ The game's own 2×/4× MSAA and 16× anisotropic filtering | on top of the scale |
| ✅ 120 fps with correct game speed | a 240 Hz virtual vblank (`-FrameRate`), measured: a steady 1,200 frames per 10 s ([PROGRESS](docs/PROGRESS.md#play-test-2-2026-10-22)) |
| ✅ Logic, gunplay, physics intact | 10-minute play-test |
| ⏳ Slight frame-pacing jank, input occasionally behind | diagnosed: unsynced software vblank + immediate present; `-Present` test option |
| ✅ Chapter cards, intro text and credits drawn once and centred; no half frames at cuts | a clear the game does through an aliased surface now reaches the real one ([PROGRESS](docs/PROGRESS.md#two-tile-frames-doubled-cards-and-half-frames)) |
| ✅ Right stick without the 360's turn ramp (default) | "feels significantly better": the 360's stick tuning patched on the fly from your own data; `-LookRamp console` restores it ([PROGRESS](docs/PROGRESS.md#play-test-2-2026-10-22)) |
| ✅ Saving with any controller (to confirm at a checkpoint) | every controller drives player 1, the local profile that saves (a second XInput device used to make the pad player 2, who can't save) |
| ⏳ Movement may feel bouncier than PC | compare game-database tuning and 30 vs 120 fps |
| ✅ PC textures in place of the 360's low-res ones | a local pack built from your PC copy: **4,710 textures at 4× the pixels**, 81% of what the intro draws ([docs/TEXTURES.md](docs/TEXTURES.md)) |
| ⏳ Settings and Quit entries in the main menu | not started |
| ⏳ *F.E.A.R. Files* (Extraction Point + Perseus Mandate) | not started |
| ⏳ Linux | the renderer is Vulkan already; build untested |

Full debrief and play-test notes: [docs/PROGRESS.md](docs/PROGRESS.md). Agent handoff: [docs/HANDOFF.md](docs/HANDOFF.md).

## Build and run (Windows)

```powershell
pwsh -NoProfile -File tools/build-nx.ps1        # first time ~15 min: ReXGlue SDK from source + codegen + game
pwsh -NoProfile -File tools/launch.ps1 -Nx -Extra '--nx_renderer native'
```

Launcher defaults: fullscreen, `-Scale 3` (4K internal), `-FrameRate 120`, 16× AF. Options: `-Scale 1..3`, `-FrameRate 30|60|120`, `-Present immediate|mailbox|fifo`, `-TexturePack <folder>`, `-LookRamp console|nodelay|instant|off`, `-NoSoftLock`, `-Windowed`, `-Extra '<more flags>'`.

**PC textures (optional, needs the PC version):** build a pack from your own PC install once (about 6 minutes, 1.3 GB, stays in the gitignored `private/`), then add `-TexturePack private/texture_pack`. Steps in [docs/TEXTURES.md](docs/TEXTURES.md#build-a-pack).

Put your own `default.xex` (title `565507D9`, SHA-256 `e7fccdf6…2792`) and `Game/` folder in `port/assets/` ([docs/RIPPING.md](docs/RIPPING.md); `python -I tools/xdvdfs.py <disc.iso> extract port/assets` unpacks an ISO). The SDK source recipe is in [docs/HANDOFF.md](docs/HANDOFF.md#how-it-is-built-and-run).

## Layout

```
port-nx/          the working build: app glue (src/), fear-nx renderer + our changes (src/native/), manifest
port/             stock-SDK build, kept for A/B (menus corrupt)
patches/          our ReXGlue SDK patch (static GPU plugin)
tools/            build, launch, smoke/autoplay tests, window capture, ISO extractor, texture pack builder
docs/             progress, handoff, findings, plan, ripping
```

## Legal

- No assets, executables, generated code or proprietary code are distributed.
- `port-nx/src/native/` is GPL-3.0-or-later (fear-nx ← Project8Recomp ← nfsmw-nx), so builds that link it are GPL. ReXGlue is BSD-3-Clause with Xenia's notices.
- Our own tools, docs and glue are MIT ([LICENSE](LICENSE), [NOTICE](NOTICE)).
- F.E.A.R. is a trademark of Warner Bros. Entertainment / Monolith Productions. Not affiliated with Monolith, Day 1 Studios, Warner Bros., Vivendi/Sierra, Activision or Microsoft.

## Credits

- Monolith Productions and Day 1 Studios: F.E.A.R. and its Xbox 360 port
- infriker: [fear-nx](https://github.com/infriker/fear-nx), the function hints and the Vulkan renderer that made the menus work; R-drg ([Project8 SW](https://github.com/R-drg/project8-sw)) and StevensND ([nfsmw-nx](https://github.com/StevensND/nfsmw-nx)) before them
- Tom Clay and contributors: [ReXGlue](https://github.com/rexglue/rexglue-sdk); the [Xenia](https://github.com/xenia-project/xenia) team
- Inspiration: [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp), [Lost Odyssey Recomp](https://github.com/freefrank/LostOdysseyRecomp), [OpenCE](https://github.com/OpenCommunityEdition/OpenCE), [EchoPatch](https://github.com/Wemino/EchoPatch)
- Built with [Claude Code](https://claude.com/claude-code). Play-tested by a human.
