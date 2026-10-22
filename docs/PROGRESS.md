# Progress Debrief

_Last updated 2026-10-21._

## Where we are
F.E.A.R. for Xbox 360 is **playable natively on Windows at 4K and 120 fps**. Menus, the intro and menu videos, the campaign and Instant Action all work; game logic, gunplay and physics are the console's own and behave correctly. Hands-on after ~10 minutes: "guns responded very well… game logic is in there, the physics are in there". The resolution, AA and anisotropic filtering made "a huge difference. Very clean image".

What's left before it feels finished: **textures** (the 360 shipped low-res ones; PC replacements look feasible), a little **frame-pacing jank**, two **presentation glitches** (chapter cards, cutscene transitions), and **Settings / Quit** in the main menu.

## What was done

### 1. Dump and recon
- Disc dumped (Redump-sized ISO), game partition extracted with an XDVDFS reader. Executable: title `565507D9`, v0.0.0.1, XDK 2.0.3529, built 2006-09-19. Details: [FINDINGS.md](FINDINGS.md).
- Prior art: [fear-nx](https://github.com/infriker/fear-nx) runs this exact executable on Switch with ReXGlue, and its manifest provides the 78 function entries the analyser misses.

### 2. First build on stock ReXGlue (`port/`)
- `rexglue init` + fear-nx's hints → 405 generated files, 215 MB of C++, built with clang on 12 jobs. Needed `config.gpu_plugin = "xenos"` to draw anything.
- Gameplay rendered correctly, but **menus and videos showed the game's font sheet**, and menu text had the wrong glyphs. Every relevant switch in the stock renderer was tried; none fixed it ([FINDINGS.md](FINDINGS.md)).

### 3. fear-nx's renderer on Windows (`port-nx/`)
- Built fear-nx's patched ReXGlue (`c94f5eb` + their SDK patch, tree hash verified) from source with Vulkan, linked the Xenos plugin statically (our small SDK patch) and compiled their renderer into `fear.exe`.
- **Menus, videos and text render correctly.** The stock renderer on the same SDK still breaks, so the fix is their renderer.
- Build gotchas solved in `tools/build-nx.ps1`: VS DevShell, `-march=x86-64-v2`, compiler-rt builtins for a 128-bit divide, one include path.

### 4. 4K
- fear-nx's renderer drew at the game's 1280×720 only. Added **integer resolution scaling** (`nx_native_resolution_scale` 1–4): render targets, viewports, scissors, clears, resolve copies, resolve images and the swap are scaled; guest-space math is untouched. 3× = 3840×2160. The game's own MSAA and 16× AF apply on top.

### 5. Frame rate
- The game presents every 2 virtual vblanks: 30 fps at 60 Hz, as on the console. Raising the virtual vblank rate (`video_mode_refresh_rate`) to 120/240 Hz gives **60/120 fps; both hands-on verified with correct game speed**. 120 is the launcher default and the ceiling of this trick (the SDK clamps at 240 Hz).

## Play-test 1 (2026-10-21, ~10 minutes, 3× scale, 120 fps)
| Finding | Notes / next step |
|---|---|
| Logic, gunplay, physics | All good |
| Image | Very clean with 4K + MSAA + 16× AF |
| **Textures are very low-res** | The 360's memory budget. See "PC textures" below |
| **Chapter cards (loading screens) off-centre and shown twice**, a couple of times | Probably a path our scaling misses (a resolve copied or sampled at guest size). Compare at `-Scale 1`. Screenshots were mentioned but not received |
| **Subtle flicker at cutscene ↔ gameplay transitions** (when control is taken away, chapter cutscenes) | Compare at `-Scale 1` and `-FrameRate 30` to see whether it's ours or fear-nx's |
| **"A little bit of jank", input occasionally behind** | Diagnosed below; `-Present` test option added |
| Menu needs Settings and Quit | Not started; routes in [HANDOFF.md](HANDOFF.md#next-steps-the-users-list) |

## Frame pacing: diagnosis
- The log shows the swapchain in **presentation mode 0 = IMMEDIATE**: no vsync and no triple buffering. The SDK prefers it for latency (`vulkan_presenter.cpp`).
- The game's frames are paced by fear-nx's **software vblank thread** (`VblankLoop`, a host timer at `video_mode_refresh_rate`), which is **not locked to the monitor's real refresh**. Presented immediately, frames reach the 240 Hz display at slightly uneven intervals and can tear. That reads as occasional jank even at a high average. Windowed-fullscreen Vulkan on Windows also goes through the compositor, so G-SYNC/VRR is unlikely to be smoothing it.
- **Test now:** `tools/launch.ps1 … -Present fifo` (true vsync: even, slightly more latency) and `-Present mailbox` (no tearing, low latency, still unsynced).
- **Real fix (next):** drive the virtual vblank from the display's actual vblank (e.g. DXGI `IDXGIOutput::WaitForVBlank` on Windows) instead of a timer, so the game's "every 2 vblanks" lands exactly on every 2nd refresh at 240 Hz. Or present to an exclusive/independent-flip swapchain so VRR engages.

## PC textures: feasibility
- The 360 keeps its textures in `Game/uberCatalog.cat` as `.tex` entries; the PC (Ultimate Shooter Edition) keeps them in `FEAR*.Arch00` as `.dds`. `tools/texture_names.py` compares the names: **6,073 of the 360's 6,252 texture names (97%) exist on PC.** The 360-only ones are its own content (controller prompts, autoshot UI, laser sight, Dell product placement…), which keep their 360 textures.
- So the upgrade is a **name-based replacement**: when the game loads `X.tex`, the renderer uses the PC's `X.dds` instead. The renderer, not the game, holds the big texture, so the 360's 512 MB memory limit doesn't apply.
- Work: (1) parse the 360 catalogue's `.tex` entries (header + data) and the PC `.Arch00` format; (2) learn which guest texture is which file, either by hooking the game's texture-load routine (reverse engineering) or by hashing each `.tex` payload offline and matching it when the renderer uploads it; (3) load the PC DDS (DXT/BC formats, more mips) in fear-nx's texture cache. Steps 1 and 3 are straightforward; step 2 is the research.

## Direction: recomp, or 360 features into the PC version?
The question came up after the play-test: would it be easier to take the 360-only features to the PC version, the way the Quake 4 project took the 360 controls to openQ4?

| | **Recomp (this project)** | **PC version + 360 features** |
|---|---|---|
| Engine | the 360's, recompiled; ours to change | PC `FEAR.exe`/`EngineServer.dll`, **closed**. No open engine like openQ4; fan fixes come from [EchoPatch](https://github.com/Wemino/EchoPatch) (high fps, widescreen, XInput) |
| Game code | the 360's, all of it, already running | the F.E.A.R. Public Tools SDK has game DLL source (objects, AI, menus, HUD) under its EULA; 360-only behaviour would have to be re-implemented from the PPC code |
| 360 content (Instant Action, machine pistol, bonus level) | already there | must be extracted from 360 formats (tiled textures, 360-built world files) and converted to PC formats; world conversion is the unknown |
| Textures | low-res; PC replacements by name look very feasible | already high-res |
| Frame rate, resolution | done (4K, 120 fps) | EchoPatch already gives up to 240 fps |

**Recommendation: stay with the recomp.** The 360 content is the reason for the project, and it's free here; going the other way means rebuilding that content and logic on a closed engine. The PC version's real advantage, its textures, can be brought *into* the recomp by name. Quake 4 was different: openQ4 is an open engine and the 360's extras were small code routines.

## Open items and next steps
1. **Frame pacing:** test `-Present fifo` / `mailbox`; then lock the virtual vblank to the display (the real fix).
2. **Chapter cards / transition flicker:** reproduce at `-Scale 1` and `-FrameRate 30` to see whether they're ours (scaling) or upstream; fix accordingly.
3. **Main menu Settings and Quit:** see [HANDOFF.md](HANDOFF.md#next-steps-the-users-list) for the two routes (host overlay vs in-game menu entry).
4. **PC texture replacement:** catalogue parsing, the name↔texture mapping, loader in the texture cache.
5. ***F.E.A.R. Files*:** dump the disc, find its executable(s), generate a second translation against the same runtime and renderer.
6. **Linux:** the renderer and SDK are Vulkan; port the build script.

## Sister projects
- `../n3-360-recomp`: N3: Ninety-Nine Nights on WK2D's ReXGlue recomp, parked at 4K / 40 fps.
- `../quake4-360-recomp`: Quake 4 Definitive Edition (openQ4 + the 360 controller feel).
