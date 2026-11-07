# Plan: 360 features on the PC version of F.E.A.R. (issue #133)

Status: milestone 1 built, waiting on its play test (2026-11-06): [PC-PROGRESS.md](PC-PROGRESS.md), findings in [PC-FINDINGS.md](PC-FINDINGS.md).

[Issue #133](https://github.com/Juansolo457/fear360-recomp/issues/133) asks for the Xbox 360's Instant Action mode in the original PC game. This plan covers that plus the rest of what you listed: controller support, Xbox and PlayStation glyphs, the game switcher and the Recompilation Settings, across F.E.A.R., Extraction Point and Perseus Mandate.

## Direction (2026-11-06, after the first play tests)

The PC version is the one to build on: it has more going on than the 360 (its AI is more fluid and harder; the 360's was tuned down). The goal is the definitive F.E.A.R. on PC, a free fan project, by porting the console versions' features to it. In order, as players on Reddit and YouTube ask most:

1. **Controller support** from the 360 version (in progress: `pc-controller` branch, issues #137 to #140).
2. **Instant Action** from the 360 version.
3. **The console-only content**: the weapons the 360 and PS3 versions added.
4. Later: **multiplayer** back in working order, and **leaderboards**, starting with a local, single-player time attack for Instant Action (personal best times, kept by a small local service, perhaps a lightweight Rust program started with the game).

Open: what a player needs to own for which content. The PC game always; the 360 disc for Instant Action and the 360's content; the PS3 disc for the PS3's. How to explain that to players is undecided.

## What I found first (it changes the plan)

1. **This is not a recompilation.** The PC game already runs natively. The work is a mod of the PC game code: rebuilt game DLLs where we have source, hooks where we don't. I'd call it "F.E.A.R. PC Enhanced" (name open) rather than a recomp, so nobody expects a new executable.
2. **We have source for the base game only.** The v1.08 public tools ship with the Steam copy (`extras\fear_publictools_108.exe`, not installed) and contain the GameClient, GameServer and ClientFX source. Extraction Point and Perseus Mandate have their own, larger game DLLs (TimeGate builds from 2006 and 2007) and no source. The engine (`FEAR.exe`, renderer linked in) is closed for all three.
3. **The expansions share the base game's folder on both stores.** Steam lists three apps (21090 F.E.A.R., 21110 Extraction Point, 21120 Perseus Mandate), but all three install into `FEAR Ultimate Shooter Edition`, and the expansion apps are 0-byte entries: their files (`FEARXP\`, `FEARXP2\`) come with the base app. GOG's Platinum Collection is one product with the same `FEARXP\` and `FEARXP2\` subfolders. Every Steam and GOG owner therefore has all three, so "must own all three" costs users nothing, and one path locates all three games. "F.E.A.R. Files" exists only as the 360 disc.
4. **Instant Action content is 360-only.** The maps (`InstantAction1`-`4`, five `IA_*` maps for Extraction Point, three for Perseus Mandate) are 360-built world files, and the rules live in `PitFight*` classes that exist only in the 360 executables. Neither has been analysed yet. `docs/PROGRESS.md` already flagged world conversion as the unknown.
5. **Toolchain.** Every PC binary is 32-bit x86 built with MSVC 7.1 (`msvcr71`/`msvcp71`). `FEAR.exe` has the Steam wrapper; `FEARXP.exe` and `FEARXP2.exe` are SecuROM-wrapped. The game DLLs themselves are unprotected and stored uncompressed in the `FEARE*.Arch00` archives.
6. **EchoPatch already gives PC users** controller support, HUD scaling and high-frame-rate fixes through a proxy DLL. What nobody offers is Instant Action, proper glyphs, the switcher and the 360 tuning (aim assist, look acceleration, bloom, enemy AI).

## What a user has to own

Nothing from either game is ever shipped, 360 or PC. What a user needs depends on what they want to run:

| They want | They need |
| --- | --- |
| Controller support, glyphs, settings on PC F.E.A.R. and its expansions | The PC game only (one Steam or GOG purchase covers all three). The 360 stick handling is re-written from measurements, so no 360 file is involved. |
| Instant Action in PC F.E.A.R. | The PC game plus their own F.E.A.R. 360 disc image, for the maps |
| Instant Action in the PC expansions | The PC game plus their own F.E.A.R. Files 360 disc image |
| The 360 recomps | The 360 discs, as today |

So the most anyone needs is three things: the PC collection and the two 360 discs. With all three, the switcher lists every version: F.E.A.R. and F.E.A.R. Files (360 recomps) and F.E.A.R., Extraction Point and Perseus Mandate (PC). Entries whose files are missing are hidden.

On Linux and Steam Deck the PC games run through Proton, because the PC engine is a closed 32-bit Windows program; the 360 recomps stay native. That comes after Windows.

## Milestone 1: the 360's controller on Windows PC F.E.A.R. (start here)

Goal: play the base PC game with a pad and have it feel like the 360 version. No Instant Action, no expansions, no switcher yet. It is the cheapest way to prove the source build works, and everything later stands on it.

Steps, in order. Each ends with something that can be checked.

1. **Dev copy.** Copy the GOG install to a working folder outside both store libraries (path recorded in `private/pc.toml`, gitignored) and remove EchoPatch's `dinput8.dll` and `EchoPatch.ini` from the copy. The GOG `FEAR.exe` has no Steam wrapper, so a debugger attaches cleanly. Both store installs stay untouched.
   Check: the copy starts and loads a save.
2. **Source tree.** Unpack the v1.08 public tools into `third_party/fear-publictools/` (gitignored), from the Steam copy's `extras\fear_publictools_108.exe`. Record the game-code file list and hashes in `docs/PC-FINDINGS.md`. Extend `tools/audit_repo.py` to reject that source.
   Check: audit fails on a staged SDK file and passes without it.
3. **Unmodified rebuild (spike 1).** `pc/tools/build.ps1` builds `GameClient.dll`, `GameServer.dll` and `ClientFx.fxd` as 32-bit. Try the current MSVC x86 toolset first; if the game crashes on the runtime mismatch with the engine's MSVC 7.1 interfaces, switch to building against the 7.1 runtime. Fixes needed to compile go in `pc/game/` as a patch series.
   Check: a build with zero behaviour changes.
4. **Loading (spike 4).** Find how to make the engine prefer our DLLs: an extra archive or loose folder listed last in a private `Default.archcfg`, selected from the command line. `pc/tools/launch.ps1` starts the dev copy that way.
   Check: the game runs our rebuilt DLLs (a version string we add shows in the console) and plays Interval 01 start to finish.
5. **Read the PC input path.** Map how the 1.08 source turns device input into look and move (`ClientShellDLL`: bindings, player camera, move manager), and whether it has Xbox 360 code paths behind platform defines. Write it up in `docs/PC-FINDINGS.md`.
6. **Pad input.** A small `pc/core/` library (no engine types, unit-tested) reads the pad through SDL3: sticks, triggers, buttons, rumble, hot-plug. The game client feeds its output into the look and move code from step 5. Default bindings match the 360 layout.
   Check: every 360 action works from the pad, menus included.
7. **360 stick feel.** Port the look handling the recomp already documents and tunes (`port-nx/src/look_tuning.*`, the Look acceleration modes, dead zones, aim assist) into `pc/core/`, re-written from the measured behaviour, with the modes exposed as console variables for now.
   Check: turn rate against stick deflection, and time to full speed, measured in the PC build and in the recomp with the same script; the curves match within a stated tolerance.
8. **Play test.** You play Interval 01 and 02 on both and say whether it feels the same. Findings go in `docs/PC-PROGRESS.md`.

Out of scope for this milestone: glyphs (prompts still show key names), the settings screen, expansions, Instant Action, Linux.

Stop and report instead of working around it if: step 3 cannot produce DLLs the engine accepts with either toolset, or step 4 needs a write into the game folder.

## Approach

Two layers, one shared core:

| Layer | Covers | How |
| --- | --- | --- |
| **Source build** | F.E.A.R. base | Patches against the user's own public tools source, built into replacement `GameClient.dll`, `GameServer.dll`, `ClientFx.fxd`. Deep changes: menu entries, Instant Action rules, input. |
| **Hook module** | Extraction Point, Perseus Mandate | One DLL loaded into the game, hooking their game DLLs at verified sites (the fingerprint and tripwire scheme from `port-nx/src/hook_guard.h`). The 1.08 source tells us what each function does. |
| **Shared core** | all three | Plain C++ with no engine types: SDL3 pad input, controller type and glyph choice, settings file, Instant Action scoring. The source build and the hook module are thin adapters over it. |

Why not source-only: there is no expansion source, and running expansion content under base-game DLLs would break (the expansion DLLs are 30-40% larger and carry their own weapons, enemies and FX).

Why not hooks-only: the base game is where most people play, and source lets us add real menu screens and game rules instead of fighting the binary.

## Where it lives

Recommended: **this repo**, in a new top-level `pc/` folder. It keeps the stars, the issue tracker (#133 is already here) and the analysis tools the PC work depends on (`tools/gamedb.py`, `tools/texture_pack.py`, the 360 executable map). Proposed layout:

```
pc/
  core/        shared core (input, glyphs, settings, IA scoring) + unit tests
  game/        patches against the public tools source (git format-patch series)
  hooks/       hook module for EP and PM, hook-site tables per executable hash
  launcher/    collection menu and game switcher
  assets/      our own glyph art and menu strings (no game data)
  tools/       setup script: install public tools, apply patches, build
docs/PC-*.md   findings, with the DLL hashes they apply to
```

Rules carried over and extended:

- The public tools source never enters the repo. `tools/audit_repo.py` learns to reject it, the same way it rejects game data today.
- Releases stay source only until the tools licence is read (spike 6).
- This machine's Steam install stays read-only. Development uses a copy of the install on another drive.

## Phase 0: spikes (about a week; each one can stop or reshape the plan)

Spikes 1 and 4 run inside milestone 1. Spikes 2 and 3 (Instant Action) can run alongside it; 5 and 6 wait.

| # | Question | Done when |
| --- | --- | --- |
| 1 | Do game DLLs built from the 1.08 source with a current compiler run under retail `FEAR.exe`? The engine and DLLs share C++ interfaces and the MSVC 7.1 runtime. | A rebuilt, unmodified set loads a save and plays a level. If the modern compiler fails on the ABI, fall back to the MSVC 7.1 toolset. |
| 2 | How does a 360 world file differ from a PC one? | A written format comparison of `InstantAction1` against a PC world, and a verdict: convert, rebuild with WorldEdit from PC pieces, or not feasible. |
| 3 | What are the Instant Action rules? | `PitFight*` classes mapped in the 360 executable: scoring, timers, loadouts, the results screen, the database records they read. |
| 4 | Can we load our DLLs without touching the install? | A launcher starts each game with an override archive list or user folder. Confirmed for all three, including the SecuROM-wrapped expansion executables. |
| 5 | How hookable are the expansion DLLs? | Class and function match rate between the 1.08 source and each expansion DLL (RTTI names), and ten sample hook sites located in each. |
| 6 | What does the public tools licence allow? | A short note: may we publish patches, may users share built DLLs. |

Spikes 2 and 3 decide whether Instant Action is weeks or months. Spikes 1 and 4 decide whether anything else starts.

## Phase 1: foundation

- `pc/tools/setup.ps1`: finds the install (Steam, GOG, or a path), unpacks the public tools into a gitignored folder, applies `pc/game/` patches, builds x86.
- Launcher that runs a game with our overrides and never writes into the install.
- Startup check of game DLL hashes; unknown versions run unmodified with a clear message.
- CI: build the core and run its tests; audit for SDK source and game data.

Verify: unmodified rebuilt base game plays through Interval 01; both expansions start through the launcher with the hook module loaded and no hooks active.

## Phase 2: controllers and glyphs

- SDL3 gamepad input in the shared core: Xbox, DualShock 4, DualSense, Switch Pro, Steam Deck. Hot-plug, rumble.
- 360 stick handling carried over from what the recomp already measured: look acceleration modes, dead zones, aim assist. Menus fully navigable by pad.
- Glyphs: controller type from SDL selects a prompt set (Automatic, Xbox, PlayStation, Nintendo, Steam Deck, keyboard), as `quake4-definitive` does. Prompts switch live when the active device changes, which the recomp cannot do.
- Glyph art is our own, so it can ship in the repo. The 360's font atlases are game data and stay out.

Verify: each pad type shows its own prompts in the HUD hints, menus and the weapon wheel in all three games; unit tests for the device-to-glyph mapping and stick curves.

## Phase 3: settings

A "PC Enhanced Settings" entry in each main menu. Base game: a native menu screen from source. Expansions: the same options through the hook module (native screen if the menu hooks allow, overlay if not).

Carried over where they make sense on PC: look acceleration, aim assist, field of view, bloom (360 style), enemy AI (PC / 360 / Sharp), weapon slots, automatic shotgun, flashlight battery, intro videos, prompt style, high-frame-rate fixes. Dropped: everything that only existed because of the recomp renderer (resolution scaling, pipeline warm-up, texture pack). Settings persist to a TOML file in the user folder, one per game, same format as `settings.toml` today.

## Phase 4: game switcher

- The launcher shows the collection menu: F.E.A.R., Extraction Point, Perseus Mandate, Quit. On PC the expansions get their own entries instead of the 360's combined "F.E.A.R. Files".
- "Switch Game" in each main menu restarts into the launcher, reusing the wait-for-exit logic from `port-nx/src/fear_process.*`.
- Direct entries for "Instant Action" per game once phase 5 lands.

## Phase 5: Instant Action, base game

Depends on spikes 2 and 3.

- **Maps**: by the route spike 2 picks. If converted from the 360 disc, the converter is a tool the user runs on their own disc image, like the texture pack today; nothing converted is ever distributed.
- **Rules**: a new game mode in the source build, reimplementing the `PitFight` behaviour: loadout, enemy waves as placed in the map, score from kills, accuracy, time and health, no checkpoints.
- **UI**: map select, HUD score and timer, results screen, local best scores. The 360's leaderboards are not reproduced.
- Scoring lives in the shared core with unit tests against values recorded from the 360 version.

Verify: all four maps playable start to finish, scores matching the 360 for a recorded run within a stated tolerance.

## Phase 6: Instant Action, expansions

The eight expansion maps, driven by the hook module: map load, kill and damage events, and end-of-round feed the same shared scoring. This is the riskiest phase because both the content and the code are closed; it starts only after phase 5 ships and spike 5 looks good.

## Phase 7: release

- Source-only release, setup script, `docs/PC-SETUP.md`.
- Compatibility note for EchoPatch: either detect it and leave its features alone, or document that the two should not be combined.
- README section and a reply on #133.

## Risks

| Risk | Effect | Mitigation |
| --- | --- | --- |
| 360 worlds cannot be converted | No Instant Action maps | Rebuild from PC campaign pieces with WorldEdit, or ship rules with new arenas |
| Rebuilt DLLs are ABI-incompatible | No source build | MSVC 7.1 toolset; worst case hooks for the base game too |
| SecuROM rejects an injected module | No expansion support | Load through the game's own DLL search rather than injection; EchoPatch shows it works |
| Licence forbids publishing patches | Distribution changes | Ship a script that makes the edits instead of patches containing source lines |
| Multiplayer anti-cheat (PunkBuster) | Bans or kicks | Single-player only; the launcher never starts `FEARMP.exe` |
| Scope | Recomp work stalls | Phases ship independently; phase 2-4 are useful without Instant Action |

## Questions

:::question
Where should this live?

- [ ] This repo, under `pc/` — keeps the stars, issue #133 and the shared analysis tools
- [ ] A new repo — cleaner history and licence story, starts at zero stars

Recommended: This repo, under `pc/`
:::

:::question
Instant Action maps come from the 360 disc. Is it acceptable that PC users need their own 360 disc image for that mode?

- [ ] Yes — a converter runs on the user's own disc, as the texture pack does
- [ ] No — rebuild the arenas from PC assets with WorldEdit, even if they differ from the 360's
- [ ] Decide after spike 2 shows what conversion costs

Recommended: Decide after spike 2 shows what conversion costs
:::

:::question
How far should expansion support go, given there is no source for them?

- [ ] Hooks for everything, including Instant Action (phase 6)
- [ ] Hooks for controllers, glyphs, settings and switcher only; Instant Action base game only
- [ ] Base game first, decide on expansions after it ships

Recommended: Base game first, decide on expansions after it ships
:::

:::question
What about EchoPatch, which many PC players already use?

- [ ] Coexist — detect it and skip the features it already provides
- [ ] Stand alone — document that the two are not combined
- [ ] Ask its author about contributing the controller and glyph work upstream

Recommended: Coexist — detect it and skip the features it already provides
:::

:::question
Should the recomp keep getting features while this is built?

- [ ] Recomp goes to maintenance (fixes only) until PC phase 4
- [ ] Both in parallel
- [ ] PC work only after the next recomp release

Recommended: Recomp goes to maintenance (fixes only) until PC phase 4
:::

:::question-text
You told the reporter "sometime next week". Phase 0 alone is about a week and Instant Action sits behind it. What should the reply on #133 promise?

Recommended: A status update next week with the spike results, not the feature
:::
