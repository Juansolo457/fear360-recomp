# F.E.A.R. 360 Recompiled

The Xbox 360 F.E.A.R., running natively on PC. 4K, evenly paced high frame rates, a controller in your hands.

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
| ✅ Boots, menus, intro videos, campaign, Instant Action | `port-nx`, with fear-nx's native renderer (the default) |
| ✅ 4K: integer internal resolution scaling (3× = 3840×2160) | our patch to fear-nx's renderer |
| ✅ The game's own 2×/4× MSAA and 16× anisotropic filtering | on top of the scale |
| ✅ Frame pacing locked to the display, up to 120 fps | the game's vblank follows the display's real one (DXGI, any GPU vendor), each frame held to the fewest refreshes that keep it at or under 120 fps (120 on 240 Hz, 72 on 144 Hz, 60 on 60 Hz); `-FrameRate <n>` sets the limit ([PROGRESS](docs/PROGRESS.md#play-test-4-2026-10-22-display-locked-pacing)) |
| ✅ The console code's high-frame-rate bugs fixed so far | enemies update every frame above 100 fps (#3); bullet tracers no longer stick in mid-air (#27); animations at exactly the time that passed |
| ✅ No white flash as a new area comes into view | textures still streaming in are waited for instead of drawn as the empty texture (to confirm hands-on) |
| ✅ Logic, gunplay, physics intact | 10-minute play-test |
| ✅ Impact smoke and bullet holes at 80 fps | the dust thins out as it should, and model animations run at exactly the time that passed (the engine dropped a fraction of a millisecond every frame); the big "rock" craters are the game's own bullet-hole decals, the same size at 30 fps ([PROGRESS](docs/PROGRESS.md#play-test-4-2026-10-22-display-locked-pacing)) |
| ✅ No haze near bright lights | it was the 360 version's screen bloom, which the PC version doesn't have: off by default, *PC Settings → Bloom* turns it back on (#38) |
| ✅ Chapter cards, intro text and credits drawn once and centred; no half frames at cuts | a clear the game does through an aliased surface now reaches the real one ([PROGRESS](docs/PROGRESS.md#two-tile-frames-doubled-cards-and-half-frames)) |
| ✅ Right stick without the 360's turn ramp (default) | "feels significantly better": the 360's stick tuning patched on the fly from your own data; `-LookRamp console` restores it ([PROGRESS](docs/PROGRESS.md#play-test-2-2026-10-22)) |
| ✅ Saving with any controller (to confirm at a checkpoint) | every controller drives player 1, the local profile that saves (a second XInput device used to make the pad player 2, who can't save) |
| ⏳ Movement may feel bouncier than PC | compare game-database tuning and 30 vs 120 fps |
| ✅ PC textures in place of the 360's low-res ones | a local pack built from your PC copy: **4,710 textures at 4× the pixels**, 81% of what the intro draws ([docs/TEXTURES.md](docs/TEXTURES.md)) |
| ✅ *PC Settings* and *Quit to Desktop* in the main menu | "looks awesome": two entries added to the game's own menu (under Options) open a settings panel (resolution, window, frame rate, texture filtering, PC textures, field of view, right stick, aim assist, animation timing, bloom; saved in your user folder) and a quit confirmation ([PROGRESS](docs/PROGRESS.md#main-menu-pc-settings-and-quit-to-desktop)) |
| ✅ No first-run stutter | "no issues in any of the loads of any of the levels": the first start on a new PC or driver builds every shader and pipeline seen in play before the main menu, the shaders rebuilt from your own disc's files (#53, `port-nx/src/shader_warmup.h`) |
| ✅ One collection: F.E.A.R. and *F.E.A.R. Files* from one menu | "switcher works with controller and switch is fine between games": a menu before either game loads, *Switch Game* in both games' main menus, *PC Settings* and *Quit* in Files' too (`tools/launch.ps1 -Collection`) ([FILES](docs/FILES.md#the-collection-launcher-2026-10-22)) |
| ⏳ *F.E.A.R. Files* (Extraction Point + Perseus Mandate) | plays at 4K and 80 fps with its own PC texture pack (7,607 textures) and F.E.A.R.'s right-stick fix: "running really well… ran better out of the box than Fear 1 did"; next, the smoke plumes and an optimisation pass. The plan for one collection: [docs/FILES.md](docs/FILES.md) |
| ⏳ Steam Deck (Proton), experimental | median 89 fps on a Deck OLED, heaviest scenes about 74 (#28, #40); `tools/steamdeck/README-SteamDeck.txt` |
| ⏳ Linux | the renderer is Vulkan already; build untested |

Full debrief and play-test notes: [docs/PROGRESS.md](docs/PROGRESS.md). Agent handoff: [docs/HANDOFF.md](docs/HANDOFF.md). How it all fits together, where the time goes, and how to help: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md).

## Build and run (Windows)

Releases are source only: you build the game from your own discs. **Step by step: [docs/SETUP.md](docs/SETUP.md)**; what's in 1.0: [docs/RELEASE-1.0.md](docs/RELEASE-1.0.md). In short, with the toolchain in `.tools/` and your discs extracted:

```powershell
pwsh -NoProfile -File tools/setup-sdk.ps1                      # the patched ReXGlue SDK
pwsh -NoProfile -File tools/build-nx.ps1                       # F.E.A.R.
pwsh -NoProfile -File tools/build-nx.ps1 -Project port-files   # F.E.A.R. Files
pwsh -NoProfile -File tools/launch.ps1 -Collection             # the collection menu
```

Launcher defaults: fullscreen, `-Scale 3` (4K internal), `-FrameRate auto` (locked to your display's refresh, at most 120 fps), 16× AF. Options: `-Scale 1..3`, `-FrameRate auto|<20-240>` (the limit), `-Timer` (the old software vblank timer instead), `-Present auto|immediate|mailbox|fifo`, `-TexturePack <folder>`, `-LookRamp console|nodelay|instant|off`, `-NoSoftLock`, `-Windowed`, `-Extra '<more flags>'`.

**PC textures (optional, needs the PC version):** build a pack from your own PC install once (about 6 minutes, 1.3 GB, stays in the gitignored `private/`), then add `-TexturePack private/texture_pack`. Steps in [docs/TEXTURES.md](docs/TEXTURES.md#build-a-pack).

Your own `default.xex` and `Game/` folder go in `port-nx/assets/` (F.E.A.R., title `565507D9`) and `port-files/assets/` (F.E.A.R. Files, `565507E8`); [docs/SETUP.md](docs/SETUP.md) has the supported hashes and the ISO extraction step.

## Layout

```
port-nx/          F.E.A.R.: app glue (src/), fear-nx renderer + our changes (src/native/), manifest
port-files/       F.E.A.R. Files: its own manifest and translation, port-nx's host code
port/             stock-SDK build, kept for A/B (menus corrupt)
patches/          the ReXGlue SDK patches (fear-nx's, then ours; tools/setup-sdk.ps1 applies them)
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
