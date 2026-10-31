# F.E.A.R. 360 Recompiled

The Xbox 360 F.E.A.R., running natively on Linux and the Steam Deck. A native Vulkan renderer, evenly paced high frame rates, a controller in your hands. *F.E.A.R. Files* and the Windows build come in later releases.

> Unofficial. **No game data included.** Bring your own Xbox 360 disc.

▶ **[Video: running natively on the Steam Deck](https://youtu.be/6rP_icfY-sM)**

**Latest release: [v1.0.2](https://github.com/Juansolo457/fear360-recomp/releases/latest)**, source only (1.0.2 adds the 360's bonus level, Holiday, to the main menu): **F.E.A.R. on native Linux and the Steam Deck** (no Proton). On the Deck it runs at 120 fps most of the time and holds 60 fps or more in 99.8% of frames. You need only your F.E.A.R. disc. **Next:** the Windows build (1.5) and *F.E.A.R. Files* ([After 1.0](#after-10)).

## Why

The 360 version came out about a year after the PC original and is the more complete one: **Instant Action** mode, a dual-wieldable machine pistol and an extra bonus level that the PC never got. It has been stuck on the console ever since, at 720p and 30 fps. This project frees it, and *F.E.A.R. Files* (Extraction Point + Perseus Mandate, 360) follows in a later release, so the whole F.E.A.R. 1 package lives in one place.

## What it is

A **static recompilation**: the game's PowerPC code is translated to native x86-64 C++ ahead of time ([ReXGlue](https://github.com/rexglue/rexglue-sdk)), and a game-specific runtime stands in for the Xbox. It is not an emulator and not a decompilation.

- **CPU:** the 360's `default.xex`, recompiled. All game logic, AI and physics are the console's own.
- **GPU:** a native renderer: the game's own Direct3D calls are drawn on your GPU through Vulkan, once per draw, instead of replaying the 360 GPU's command stream tile by tile. Ported from [GRAW 360 Recompiled](https://github.com/Juansolo457/graw360-recomp), after [pgr4-native](https://github.com/shaunsingh/pgr4-native); [fear-nx](https://github.com/infriker/fear-nx)'s graphics system still keeps the GPU's timing and the display pacing. (Up to 0.1.1 the game drew with fear-nx's Vulkan renderer.)
- **Your data:** the game files come from your own disc; nothing proprietary is in this repo.

## Status

| | |
|---|---|
| ✅ Boots, menus, intro videos, campaign, Instant Action | `port-nx`, with the Direct3D 12 renderer (`port-nx/src/d3d12/`, [docs/NATIVE_RENDERER.md](docs/NATIVE_RENDERER.md)) |
| ✅ No flicker on the briefing TVs, no black bands, bullet sparks show | fixed by the Direct3D 12 renderer (#66) |
| ⏳ 4K | the Direct3D 12 renderer draws at the 360's 1280×720 for now, scaled to your screen; resolution scaling comes back next ([NATIVE_RENDERER](docs/NATIVE_RENDERER.md#native-punch-list)), FSR 1 upscaling in [#72](https://github.com/Juansolo457/fear360-recomp/issues/72) |
| ✅ The game's own 2×/4× MSAA (or off), up to 16× anisotropic filtering, sharper textures | PC Settings |
| ✅ Frame pacing locked to the display, up to 120 fps | the game's vblank follows the display's real one (DXGI, any GPU vendor), each frame held to the fewest refreshes that keep it at or under 120 fps (120 on 240 Hz, 72 on 144 Hz, 60 on 60 Hz); `-FrameRate <n>` sets the limit ([PROGRESS](docs/PROGRESS.md#play-test-4-2026-10-22-display-locked-pacing)) |
| ✅ The console code's high-frame-rate bugs fixed so far | enemies update every frame above 100 fps (#3); EchoPatch's fixes for sliding on slopes, the jump kick, speed on landing and out of water, water waves and splashes, and the slow-mo meter (#5, #6, #8, #9, #11-#13); bullet tracers no longer stick in mid-air (#27); animations at exactly the time that passed |
| ✅ Logic, gunplay, physics intact | play-tests so far; the full play-through is the 1.0 smoke test ([#56](https://github.com/Juansolo457/fear360-recomp/issues/56), [#57](https://github.com/Juansolo457/fear360-recomp/issues/57)) |
| ✅ Impact smoke and bullet holes at 80 fps | the dust thins out as it should, and model animations run at exactly the time that passed (the engine dropped a fraction of a millisecond every frame); the big "rock" craters are the game's own bullet-hole decals, the same size at 30 fps ([PROGRESS](docs/PROGRESS.md#play-test-4-2026-10-22-display-locked-pacing)) |
| ✅ No haze near bright lights | it was the 360 version's screen bloom, which the PC version doesn't have: off by default, *PC Settings → Bloom* turns it back on (#38) |
| ✅ Right stick without the 360's turn ramp (default) | "feels significantly better": the 360's stick tuning patched on the fly from your own data; `-LookRamp console` restores it ([PROGRESS](docs/PROGRESS.md#play-test-2-2026-10-22)) |
| ✅ Saving and *Continue game* | checkpoints save and *Continue game* picks them up: "was able to jump into a saved game no issues"; every controller drives player 1, the local profile that saves (a second XInput device used to make the pad player 2, who can't save) |
| ⏳ Movement may feel bouncier than PC | compare game-database tuning and 30 vs 120 fps |
| ⏳ PC textures in place of the 360's low-res ones | a local pack built from your PC copy (4,710 textures at 4× the pixels, [docs/TEXTURES.md](docs/TEXTURES.md)); the Direct3D 12 renderer can't use it yet |
| ✅ *PC Settings* and *Quit to Desktop* in the main menu | "looks awesome": entries added to the game's own menu (in their own column, with help lines like the game's, #80) open a settings panel (window, frame rate, anti-aliasing, texture filtering and sharpness, field of view, right stick, aim assist, enemy AI, EchoPatch's options, the interval select, animation timing, bloom; saved in your user folder) and a quit confirmation ([PROGRESS](docs/PROGRESS.md#main-menu-pc-settings-and-quit-to-desktop)) |
| ✅ Shaders built before play | every start builds the pipelines earlier runs stored before the game's first frame, on all cores ("PREPARING SHADERS" when it takes a moment), so a scene seen before doesn't stutter (`port-nx/src/pipeline_warmup.h`) |
| ✅ One collection: F.E.A.R. and *F.E.A.R. Files* from one menu | "switcher works with controller and switch is fine between games": a menu in the game's style before either game loads (#43), *Switch Game* in both games' main menus, *PC Settings* and *Quit* in Files' too (`tools/launch.ps1 -Collection`) ([FILES](docs/FILES.md#the-collection-launcher-2026-10-22)) |
| ⏳ *F.E.A.R. Files* (Extraction Point + Perseus Mandate) | plays on the same Direct3D 12 renderer (its Direct3D mapped on its own) with F.E.A.R.'s fixes; still to do: the smoke plumes, its own effects ([#51](https://github.com/Juansolo457/fear360-recomp/issues/51)), an optimisation pass and the full play-through ([#57](https://github.com/Juansolo457/fear360-recomp/issues/57)). The collection: [docs/FILES.md](docs/FILES.md) |
| ✅ Steam Deck, native Linux (0.9) | no Proton: Vulkan straight on the Deck's driver, ThinLTO and optional PGO. With the 360's settings at 90 Hz, under 1% of frames below 60 fps in the heaviest fights (F.E.A.R. Files' Sprint, F.E.A.R.'s Rooftop); `tools/steamdeck/README-SteamDeck-Linux.txt`, [docs/LINUX_NATIVE.md](docs/LINUX_NATIVE.md) |
| ✅ Linux | the same native build (`tools/build-linux.sh`), Vulkan 1.3 ([docs/SETUP.md](docs/SETUP.md#linux-and-steam-deck)) |
| ⏳ Windows on ARM64 | scoped, not started ([#73](https://github.com/Juansolo457/fear360-recomp/issues/73)) |
| ⏳ Android (ARM64) | scoped, not started; needs a renderer path, since the games are Direct3D 12 only ([#44](https://github.com/Juansolo457/fear360-recomp/issues/44)) |

## After 1.0

**1.0 is F.E.A.R. on Linux and the Steam Deck**, play-tested on the Deck (the smoke test so far: [docs/SMOKE-1.0.md](docs/SMOKE-1.0.md); what's in it: [docs/RELEASE-1.0.md](docs/RELEASE-1.0.md)). Next:

| | |
|---|---|
| ⏳ Bugs reported against 1.0 | a sweep of the issue board |
| ⏳ 1.5: Windows | the same renderer; resolution scaling (4K) and the PC texture pack back on it, then a Windows test pass |
| ⏳ *F.E.A.R. Files* [#57](https://github.com/Juansolo457/fear360-recomp/issues/57) | both campaigns (37 levels) and Instant Action (8 maps), each to the end; its menu entry says "coming later" until then |
| ⏳ More performance | the water's screen copies in the biggest water fights ([#103](https://github.com/Juansolo457/fear360-recomp/issues/103)) |

**Known issues** in v1.0: Interval 03's biggest water fight can dip into the 50s for a second or two ([#103](https://github.com/Juansolo457/fear360-recomp/issues/103)); the renderer draws at 1280×720 for now and can't use the PC texture pack yet ([NATIVE_RENDERER](docs/NATIVE_RENDERER.md#native-punch-list)); slow-mo firefights can drop below 120 fps; no blood marks on the Steam Deck yet; a GPU load spike in Interval 2's closing sequence ([#41](https://github.com/Juansolo457/fear360-recomp/issues/41)); docked at 120 Hz not retested yet ([#89](https://github.com/Juansolo457/fear360-recomp/issues/89)); the 360-only Holiday bonus mission not checked yet ([#39](https://github.com/Juansolo457/fear360-recomp/issues/39)); on the Steam Deck, the controller drops for about a second at story autosaves ([#36](https://github.com/Juansolo457/fear360-recomp/issues/36)).

**Other open work:** performance (the Steam Deck [#40](https://github.com/Juansolo457/fear360-recomp/issues/40), the renderer's GPU cost [#37](https://github.com/Juansolo457/fear360-recomp/issues/37), lower-end PCs [#58](https://github.com/Juansolo457/fear360-recomp/issues/58)); PC parity with EchoPatch ([#2](https://github.com/Juansolo457/fear360-recomp/issues/2)); keyboard and mouse ([#59](https://github.com/Juansolo457/fear360-recomp/issues/59)); rumble and gyro aiming ([#26](https://github.com/Juansolo457/fear360-recomp/issues/26)); Instant Action extras ([#60](https://github.com/Juansolo457/fear360-recomp/issues/60), [#61](https://github.com/Juansolo457/fear360-recomp/issues/61)); visual checks against the 360 ([#45](https://github.com/Juansolo457/fear360-recomp/issues/45) to [#52](https://github.com/Juansolo457/fear360-recomp/issues/52)); upscaling ([#72](https://github.com/Juansolo457/fear360-recomp/issues/72)); Windows on ARM64, Linux and Android ([#73](https://github.com/Juansolo457/fear360-recomp/issues/73), [#74](https://github.com/Juansolo457/fear360-recomp/issues/74), [#44](https://github.com/Juansolo457/fear360-recomp/issues/44)). [All open issues](https://github.com/Juansolo457/fear360-recomp/issues).

Full debrief and play-test notes: [docs/PROGRESS.md](docs/PROGRESS.md). Agent handoff: [docs/HANDOFF.md](docs/HANDOFF.md). How it all fits together, where the time goes, and how to help: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build and run

**Linux and the Steam Deck (1.0):** [docs/SETUP.md, Linux and Steam Deck](docs/SETUP.md#linux-and-steam-deck): your F.E.A.R. disc in `port-nx/assets`, build in the Deck container, package, then add `fear-linux.sh` to Steam (or run it on any Linux PC):

```bash
tools/build-deck.sh                                   # F.E.A.R.
python3 -I tools/steamdeck/make_package.py --linux    # private/steamdeck/FEAR360-SteamDeck-linux.zip
```

**Windows** (builds, not tested for 1.0; supported from 1.5): releases are source only: you build the game from your own discs ([the latest release](https://github.com/Juansolo457/fear360-recomp/releases/latest)). **Step by step: [docs/SETUP.md](docs/SETUP.md)**; what 1.0 will have: [docs/RELEASE-1.0.md](docs/RELEASE-1.0.md). In short, with the toolchain in `.tools/` and your discs extracted:

```powershell
pwsh -NoProfile -File tools/setup-sdk.ps1                      # the patched ReXGlue SDK
pwsh -NoProfile -File tools/build-nx.ps1                       # F.E.A.R.
pwsh -NoProfile -File tools/build-nx.ps1 -Project port-files   # F.E.A.R. Files
pwsh -NoProfile -File tools/launch.ps1 -Collection             # the collection menu
```

Launcher defaults: fullscreen, `-FrameRate auto` (locked to your display's refresh, at most 120 fps). Options: `-FrameRate auto|<20-240>` (the limit), `-Anisotropic 1..5` (2× to 16×), `-Timer` (the old software vblank timer instead), `-LookRamp console|nodelay|instant|off`, `-NoSoftLock`, `-Windowed`, `-Invulnerable` (for testing), `-Extra '<more flags>'`. Most of these are in PC Settings too.

**PC textures (needs the PC version):** the pack builder still works ([docs/TEXTURES.md](docs/TEXTURES.md#build-a-pack)), but the Direct3D 12 renderer can't use a pack yet.

Your own `default.xex` and `Game/` folder go in `port-nx/assets/` (F.E.A.R., title `565507D9`) and `port-files/assets/` (F.E.A.R. Files, `565507E8`); [docs/SETUP.md](docs/SETUP.md) has the supported hashes and the ISO extraction step.

## Layout

```
port-nx/          F.E.A.R.: app glue (src/), the Direct3D 12 renderer (src/d3d12/), fear-nx's GPU ring (src/native/), manifest
port-files/       F.E.A.R. Files: its own manifest and translation, port-nx's host code
port/             stock-SDK build, kept for A/B (menus corrupt)
patches/          the ReXGlue SDK patches (fear-nx's, then ours; tools/setup-sdk.ps1 applies them)
tools/            build, launch, smoke/autoplay tests, window capture, ISO extractor, texture pack builder
docs/             progress, handoff, findings, plan, ripping
```

## Legal

- Changes reach `master` only through reviewed pull requests: [CONTRIBUTING.md](CONTRIBUTING.md).
- No assets, executables, generated code or proprietary code are distributed. Releases are source only: never attach builds, game files or the private test packages `tools/steamdeck/make_package.py` makes. Before pushing, run `python -I tools/audit_repo.py` (CI runs it too).
- Three sets of terms, file by file in [NOTICE](NOTICE), full texts in [LICENSES/](LICENSES/):
  - **MIT** ([LICENSE](LICENSE)): our own tools, docs and glue.
  - **GPL-3.0-or-later** ([LICENSES/GPL-3.0.txt](LICENSES/GPL-3.0.txt)): `port-nx/src/native/` (what's left of fear-nx ← Project8Recomp ← nfsmw-nx: the GPU ring and display pacing), the manifests and SDK patch 0000. Builds that link it are GPL.
  - **MIT, pgr4-native** ([port-nx/src/d3d12/LICENSE.pgr4-native](port-nx/src/d3d12/LICENSE.pgr4-native)): `port-nx/src/d3d12/`, the renderer (Direct3D 12 and Vulkan), and SDK patches 0016-0017.
  - **BSD-3-Clause**: ReXGlue (with Xenia's notices), its `rexglue init` scaffolding and our SDK patches 0001 on.
- F.E.A.R. is a trademark of Warner Bros. Entertainment / Monolith Productions. Not affiliated with Monolith, Day 1 Studios, Warner Bros., Vivendi/Sierra, Activision or Microsoft.

## Credits

- Monolith Productions and Day 1 Studios: F.E.A.R. and its Xbox 360 port
- shaunsingh: [pgr4-native](https://github.com/shaunsingh/pgr4-native), the Direct3D 12 renderer this one is ported from (by way of GRAW 360 Recompiled)
- infriker: [fear-nx](https://github.com/infriker/fear-nx), the function hints, the GPU ring the renderer runs on, and the Vulkan renderer that made the menus work up to 0.1.1; R-drg ([Project8 SW](https://github.com/R-drg/project8-sw)) and StevensND ([nfsmw-nx](https://github.com/StevensND/nfsmw-nx)) before them
- Tom Clay and contributors: [ReXGlue](https://github.com/rexglue/rexglue-sdk); the [Xenia](https://github.com/xenia-project/xenia) team
- Inspiration: [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp), [Lost Odyssey Recomp](https://github.com/freefrank/LostOdysseyRecomp), [OpenCE](https://github.com/OpenCommunityEdition/OpenCE), [EchoPatch](https://github.com/Wemino/EchoPatch)
- Built with [Claude Code](https://claude.com/claude-code). Play-tested by a human.
