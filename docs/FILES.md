# F.E.A.R. Files (Xbox 360): findings and plan

*F.E.A.R. Files* (2007) puts both expansions, *Extraction Point* and *Perseus Mandate*, on one disc. This is the first look at it, from the user's own disc: hashes, names and sizes only, never content. The goal it serves is at the end: **one definitive F.E.A.R.** with both expansions reachable from its main menu.

## Disc and executable
| | |
|---|---|
| ISO | 7,835,492,352 bytes; XDVDFS game partition at `0x0FD90000`; 403 files, 6.18 GiB |
| `default.xex` | 20,561,920 bytes, SHA-256 `a51deed1dc0842af32a3a2b811674552f4ae5b6339ce8b9abe0eabe14ba6c7eb`, title `565507E8`, media `471C0077`, v0.0.0.1, built 2007-09-15 |
| Original PE name | `FEARXP2_F.exe`: the Perseus Mandate build, carrying Extraction Point too. One executable for both |
| Image | load `0x82000000`, size `0x1720000` (F.E.A.R.: `0x15C0000`), entry `0x8219DCC8` |
| XDK | 2.0.6274 (F.E.A.R.: 2.0.3529); the same libraries (D3D9/D3DX9, XACT, XUI, XHV, XONLINE, XNET, XMP) |
| Data | `Game/uberCatalog.cat` 532 MB; `Game/Worlds` 5.0 GB: `ReleaseXP` (Extraction Point, with 5 Instant Action maps: Asylum, Catacombs, Elevator, Garage, Subway), `ReleaseXP2` (Perseus Mandate), `MultiplayerXP`, `MultiplayerXP2`, `ReleaseMultiplayer`; `Game/Snd` 590 MB (music and effects, with `-XP2` sets, voice in four languages); Bink videos in `videos`, `VideosXP`, `VideosXP2` |

Unlike F.E.A.R., there is no `Game/database` folder on the disc: the game database is presumably inside the catalog.

`tools/xexinfo.py` reads these headers; `tools/xdvdfs.py` lists and extracts the disc.

## First code generation (2026-10-22)
ReXGlue 0.10 (our patched SDK), `rexglue init` + `codegen`, in a scratch project under `private/`:
- The analyser stopped at **5 tail-call targets** outside any known function: `0x827C6B58`, `0x822A4D8C`, `0x823811B8`, `0x827D8878`, `0x82A04E70`. Added as `[entrypoint.functions]` entries (the same kind of hint fear-nx found for F.E.A.R.).
- Then: **425 files, 226 MB of C++, 100,589 functions, no warnings**, 137 s. F.E.A.R. was 405 files and 215 MB.
- F.E.A.R. needed fear-nx's 78 hints for indirect-call targets found at run time; Files needs its own.

## It runs (2026-10-22)
`port-files/` builds `fearfiles.exe` from port-nx's host code (renderer, pacing, fixes) and its own `fear_manifest.toml` (`tools/build-nx.ps1 -Project port-files`, `tools/launch.ps1 -Files`). The disc data is extracted under the gitignored `private/`.
- First run: "Call to invalid or unregistered function at 0x82511420" while loading. Rather than one rebuild per crash, `tools/find_missed_functions.py` reads the loaded image (`--fear_dump_image`, now written as soon as the game sets its GPU interrupt) and lists entries the analyser missed: **105 method- and callback-table entries** (words in data pointing into code at a function boundary, next to other code pointers; 2 that turned out to be labels inside functions were dropped when codegen reported branches across them) and **19 callbacks whose addresses are built in code** (`lis`/`addi`, in gaps no function covers).
- With those: the *F.E.A.R. Files* title screen, the menus, and Extraction Point's Instant Action map *Subway*, played (and lost, and restarted) for 3 minutes by the test script with no crash, at an even 80 fps. The user took the controls during that run: "fear files got in clean, was able to play with the character in instant action… felt pretty good."
- Not tried yet: the campaigns (Extraction Point, Perseus Mandate), 4K, the texture pack.

## Getting ready for the campaigns (2026-10-22)
- `fearfiles.exe` rebuilt on the current host code: display-locked pacing, the settings file (`port-files/user/settings.toml`; the *PC Settings* entry itself isn't in Files' menu yet), the animation-time hooks.
- A texture pack of its own: 7,607 PC textures (above, and docs/TEXTURES.md).
- Missed functions: data words pointing into code that no known function covers. Files has 49 such targets; nearly all are zero words in padding (not code), the same picture as F.E.A.R. (42), which plays through. Nothing to add up front; a campaign that calls a missed function ends with a crash report naming it.
- To play: `tools/launch.ps1 -Files -TexturePack private/files/texture_pack -Extra '--nx_renderer native'` (4K from the first-run settings).

## What carries over from the F.E.A.R. port
| | F.E.A.R. | Files |
|---|---|---|
| Native Vulkan renderer, resolution scaling, AF, MSAA | done | same GPU, same engine: expected to work; F.E.A.R.-specific resolve handling to check |
| Display-locked pacing, frame-rate limit | done | host side, unchanged |
| Every controller on player 1, crash reports | done | host side, unchanged |
| Animation time without the millisecond truncation (impact puffs) | `0x82191E7C`, `0x82241604`, `0x821E4DDC` | **found:** `trk_Update` `sub_821C6A00` (`0x821C6A3C`), model update `sub_8221F1B8` (`0x8221F1EC`) and `sub_8227EEA0` (`0x8227EED4`), by `tools/find_anim_sites.py`, which also finds exactly F.E.A.R.'s three |
| Right-stick tuning (patched `Loki.gamedb` through a VFS redirect) | done | the database is not a loose file: needs a redirect into the catalog, or the patch applied in memory |
| PC texture pack | 4,710 textures | **built: 7,607 textures**, the same builder with the PC expansions' archives first (`--expansions FEARXP2,FEARXP`; docs/TEXTURES.md) |
| Tracers break above ~90 fps | open | expected the same |

## EchoPatch and the PC version
EchoPatch supports both expansions on PC. Mapped onto the 360 versions:
- **Applies (the same engine code, to find per executable):** its high-FPS fixes: Havok ragdoll and water physics, water splash repeats, frozen effects and oversized particles, velocity damping on jumps and landings, the walk animation dropping to idle (camera stutter), the jump kick, sliding on slopes, the slow-motion charge/timer desync, AI reactions. The impact puffs were one of these on the 360, through animation time (`docs/PROGRESS.md`, play-test 4). F.E.A.R.'s particle code differs from the PC's in places (the 360 dropped the 1 ms particle-update skip), so each fix is checked against the 360 code, not copied.
- **Partly fixed by Monolith already:** the weapon save/load bugs (aim after loading in a cutscene, stuck firing animation, weapon cycling over empty slots, weapon model position).
- **PC-only, not needed:** DXGI VRAM detection, DirectInput HID and hook lag, the NVIDIA shadow fix (our renderer draws), windowing, HUD scaling (the 360 HUD is made for a TV).
- **To measure:** save speed (the 360 saves through a different path).
- **PC as reference:** the SDK 1.08 game code and the LithTech runtime source match the 360 code closely; they're the map for finding each system (see `docs/HANDOFF.md`), not something to copy in.

## The definitive edition: one game
The aim: open F.E.A.R., and reach *Extraction Point* and *Perseus Mandate* from its main menu, with no disc swapping. All of it at 4K, at a high and even frame rate, with PC textures and the fixes above, including the 360-only content the PC never got.

1. **Your discs, verified once.** A first-run step points at the two discs (ISO or extracted), checks `default.xex` (SHA-256) and the file list, and extracts them into the user data folder. Nothing is distributed; it's the same rule as now.
2. **Two titles, one runtime.** Both executables load at `0x82000000`, and every translated function is named `sub_<address>`, so the two can't share one process. One host code base (renderer, pacing, fixes) builds `fear.exe` and `fearfiles.exe`. Switching titles restarts into the other, as the console does when a game launches another (`XamLoaderLaunchTitle`): a few seconds of loading.
3. **The menu.** F.E.A.R.'s main menu is built in code (on PC, the SDK's `ScreenMain`). A hook in the 360's main-screen builder adds *Extraction Point* and *Perseus Mandate*; choosing one relaunches into Files and opens that campaign. Files' menu gets *F.E.A.R.* back the same way. It's the same mechanism as the animation fix (`[[entrypoint.midasm_hook]]` calling our code). *PC Settings* and *Quit to Desktop* were added this way first (`port-nx/src/fear_menu.*`, docs/PROGRESS.md); Files' main screen needs its own addresses.
4. **Saves.** One user folder for both titles (title IDs `565507D9` and `565507E8`).
5. **One launcher for all of them (the user's idea, for after the Files campaigns):** "one global launcher with all of the games launchable from one menu instead of having to jump different games". A small front end lists F.E.A.R., *Extraction Point* and *Perseus Mandate* (and could list the other recomps), checks each disc once, and starts the right executable with the right campaign; the restart *PC Settings* uses (`port-nx/src/fear_settings.cpp`: relaunch with arguments, the new run waits for the old one to exit) carries over to switching titles from inside a game.

## Next steps
1. **Play-test Files:** both campaigns and the Instant Action maps, at 4K. Any further missed function shows up as a crash report with its address.
2. Port the remaining F.E.A.R. fixes (stick tuning through the catalog), then the PC texture pack for both expansions.
3. Go through EchoPatch's high-FPS list on both executables, one fix at a time, against the PC SDK and LithTech source.
4. Add the menu hooks and the title switch; then the first-run disc check.
