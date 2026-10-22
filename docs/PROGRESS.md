# Progress Debrief

_Last updated 2026-10-22 (play-test 2)._

## Where we are
F.E.A.R. for Xbox 360 is **playable natively on Windows at 4K**, presenting at 60 fps for now (see play-test 2). Menus, the intro and menu videos, the campaign and Instant Action all work; game logic, gunplay and physics are the console's own and behave correctly. Hands-on after ~10 minutes: "guns responded very well… game logic is in there, the physics are in there". The resolution, AA and anisotropic filtering made "a huge difference. Very clean image".

Since play-test 1 the **textures** are fixed (an optional pack built from the player's own PC copy replaces 4,710 of the 360's halved textures with the PC originals), and so are the **doubled chapter cards and half frames at cuts** (a renderer bug in how the game's tile clears were applied). What's left before it feels finished: a little **frame-pacing jank** and **Settings / Quit** in the main menu.

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
- The game presents every 2 virtual vblanks: 30 fps at 60 Hz, as on the console. Raising the virtual vblank rate (`video_mode_refresh_rate`) to 120/240 Hz lets it run faster with **correct game speed** (hands-on verified). **Correction (2026-10-22):** the renderer's own counters show ~58 presents per second at both the 120 and 240 Hz settings; the "120" sessions were really 60. See play-test 2.

### 6. PC textures (2026-10-22)
- An offline tool reads the 360 catalogue and the PC archives, matches textures by path, checks each pair with a quality gate (the PC texture scaled down must look like the 360 one), and writes a local pack. The renderer recognises each 360 texture by a hash of its data when it loads and streams the PC version in instead. Details: [TEXTURES.md](TEXTURES.md).
- **4,710 replacements** (92% of the textures with a larger PC original), including the normal maps (re-encoded to the 360's two-channel format) and the uncompressed textures. In the intro, **81% of the game textures drawn come from the pack** (393 of 485).
- Checked in game at 2×: the guard's "ATC" patch and fabric weave go from blurred to sharp; Fettel's cell floor gains its stone relief; colours (blood, vending machines) unchanged; no load failures.
- Launch with `tools/launch.ps1 -Nx -TexturePack private/texture_pack -Extra '--nx_renderer native'`.

## Play-test 1 (2026-10-21, ~10 minutes, 3× scale, 120 fps)
| Finding | Notes / next step |
|---|---|
| Logic, gunplay, physics | All good |
| Image | Very clean with 4K + MSAA + 16× AF |
| **Textures are very low-res** | The 360's memory budget. **Fixed with the PC texture pack** (section 6) |
| **Chapter cards (loading screens) off-centre and shown twice**, a couple of times | **Fixed** (2026-10-22): a ghost copy one tile to the left; the cards are centred. See "Two-tile frames" |
| **Subtle flicker at cutscene ↔ gameplay transitions** (when control is taken away, chapter cutscenes) | A frame at cuts showing the previous frame's right half: **fixed**, same cause |
| **"A little bit of jank", input occasionally behind** | Diagnosed below; `-Present` test option added |
| Menu needs Settings and Quit | Not started; routes in [HANDOFF.md](HANDOFF.md#next-steps-the-users-list) |
| **Movement feels "bouncier" than PC: more sway/bob?** (user unsure) | See "Movement feel" below: compare the two versions' tuning data, and check frame-rate dependence |

## Play-test 2 (2026-10-22, 3× scale, PC texture pack)
"The game looks much better. Everything looks, feels pretty great." Then three things.

**1. The game closed by itself** about 10 minutes in, ~10 s after a level load started. The log just stops; Windows recorded nothing because **Windows Error Reporting is disabled on this PC**. Added `src/crash_report.cpp`: an unhandled exception, the SDK's debug break (the game's `KeBugCheck` lands there), `abort()` and `exit()` (the game's `HalReturnToFirmware`) now leave `port-nx/logs/crash-<time>.txt` (exception, module+offset, symbolised stack; recompiled functions are named `sub_<360 address>`) and a small `.dmp`. Next crash will say where.

**2. Frame rate isn't 120, and isn't even.** Counters added to the 10-second report (`per frame, renderer thread: swap … (present …)`, `all submissions: queue lock …`) show where the render thread's time goes:
- GPU work is 2–5 ms per frame; texture work and GPU waits ~0. The thread spends **13–15 ms of every frame inside the present**.
- First layer: the SDK created an (idle) achievement-notification dialog at launch on Windows. Any registered UI dialog makes the presenter paint from the UI thread, whose present held the Vulkan queue lock, so our submits waited on that lock 13–15 ms. Fixed in `patches/rexglue-sdk-nx/0002` (create it on the first achievement, as fear-nx already does on Switch): queue lock wait now 0.00 ms.
- Second layer: with that gone, **`vkQueuePresentKHR` itself blocks 12–15 ms**, in immediate (no-vsync) mode on a 240 Hz display, with NVIDIA's Vulkan-over-DXGI layer on or off. Several 10-second windows show exactly 601 presents: a 60.1 fps limiter applied inside present, outside the game. RTSS/Afterburner is hooked (its OSD is in every capture) but its frame limit is off (`Limit=0`). Remaining suspects: an NVIDIA driver *Max Frame Rate* (global or per-app, or in the NVIDIA app), or an Afterburner/RTSS setting applied at runtime. To check on the user's side.
- Why it feels uneven rather than just "60": the game is paced by a 240 Hz virtual vblank and wants a frame every 2 vblanks (8.3 ms), but each present then stalls ~15 ms. Frames reach the screen at a steady 60 while the game's timing and input sampling run on a different rhythm.

**3. Right stick "seam":** "once you first start moving it… then you stop for a little bit… and move it again, you can feel almost like a slight delay." The 360 tuning has exactly the parts for that: in `Game/database/Loki.gamedb` **`GPadAimEdgeAccelTime` / `GPadAimEdgeMaxTurnRate`** (turn speed ramps up over time once the stick reaches its edge), **`TurningSoftLockYawFactor/PitchFactor`** and **`AimAssist.*`** (turning slows near targets); in the profile defaults `Game/ProfileDatabase/Defaults.gamedb` **`YawAccel` / `PitchAccel`**, **`Sensitivity`** and per-binding **`DeadZoneMin/Max`**. A ramp that restarts from zero each time the stick starts moving is the likeliest cause. Next: read their values (LithTech game-database format), check whether the game's own Controls menu exposes acceleration, and offer a "no ramp" option. It would be 360 tuning, not a recomp bug; the PC version (mouse) has no such ramp.

**Also asked for:** saving needs a signed-in profile, and Xbox Live for 360 is gone (same as N3) → give the game a local profile it accepts. And the Options and Quit entries in the main menu.

## Movement feel: 360 vs PC
The user's sense: walking feels a little more bouncy than on PC, maybe extra sway. Two ways it could be real, both checkable:
- **Different tuning on 360.** Console ports often retuned head bob, weapon sway and look acceleration for a pad. Both versions ship their tuning as a LithTech game database (360: `Game/database`, `database_anim`; PC: `FEAR*.Gamdb00p` inside `FEAR.Arch00`). Diff the player/camera/weapon records (head bob, sway, movement acceleration, look sensitivity) between them. If they differ, the 360's values are what the 360 shipped, and a setting could offer the PC's.
- **Frame-rate dependence.** The game was tuned at 30 fps; if bob/sway or camera smoothing is integrated per frame, 120 fps can change its amplitude or feel. Compare `-FrameRate 30` against `120` in the same spot. If the 30 fps run feels like PC and 120 doesn't, it's ours to fix (scale the per-frame step).
- A measured comparison beats feel: record the camera position/orientation per frame (a guest-memory read of the player camera, or screen captures while walking in a straight line) at 30 and 120 fps, and on PC with EchoPatch at 120.

## Frame pacing: diagnosis
- The log shows the swapchain in **presentation mode 0 = IMMEDIATE**: no vsync and no triple buffering. The SDK prefers it for latency (`vulkan_presenter.cpp`).
- The game's frames are paced by fear-nx's **software vblank thread** (`VblankLoop`, a host timer at `video_mode_refresh_rate`), which is **not locked to the monitor's real refresh**. Presented immediately, frames reach the 240 Hz display at slightly uneven intervals and can tear. That reads as occasional jank even at a high average. Windowed-fullscreen Vulkan on Windows also goes through the compositor, so G-SYNC/VRR is unlikely to be smoothing it.
- **Test now:** `tools/launch.ps1 … -Present fifo` (true vsync: even, slightly more latency) and `-Present mailbox` (no tearing, low latency, still unsynced).
- **Real fix (next):** drive the virtual vblank from the display's actual vblank (e.g. DXGI `IDXGIOutput::WaitForVBlank` on Windows) instead of a timer, so the game's "every 2 vblanks" lands exactly on every 2nd refresh at 240 Hz. Or present to an exclusive/independent-flip swapchain so VRR engages.

## Two-tile frames: doubled cards and half frames
Autoplay runs at 1× and 2× with `--nx_native_trace_output`, screenshots every 2–3 s through the intro:
- **The game draws each 3D frame in two tiles**, as the 360 must for 720p with 2× MSAA (the frame doesn't fit in the 10 MB of EDRAM): left tile 0–768 px, right tile 640–1280 px with a −640 window offset, each copied out to the same frame texture. The renderer joins the colour tiles correctly (a "shared" destination).
- **Doubled credits/cards:** the intro credits show every name twice, the copy exactly 640 px (one tile) to the left and cut off at the screen edge, **at 1× and at 2×**. The first tile's copy-out also clears the render target, the second's doesn't, so the second tile's pixels (shifted by −640) are still there when the next frame starts. A frame that doesn't redraw every pixel (text on black) shows them. On the console the game clears EDRAM in a way the renderer doesn't reproduce (to find: a clear the renderer skips or scopes to one tile).
- **Half frames at cuts:** at a cut from a cutscene to the credits, a frame shows the left tile drawn and the right half black (2×), or the right half grey (an earlier 2× run). Same family: one tile of the new frame next to stale contents.
- **Depth tiles aren't joined:** the 2× MSAA depth copies of the two tiles take the renderer's "independent fallback" (it joins depth only without MSAA), so an effect that samples scene depth sees the left tile only. Same at 1×.
- **None of it is our scaling** (identical traces at 1× and 2×).

**Cause, from a per-draw trace of a credits frame** (`--nx_native_trace_frame_every N`): Direct3D clears each tile mostly through a **4× MSAA alias of the surface**: the 800-pixel-wide 2× MSAA surface is cleared as a 400-pixel 4× one (same EDRAM, same samples per row, half the pixels to draw), plus a 48-pixel strip through the surface itself. fear-nx's renderer keeps one host image per surface layout, so the alias's colour and depth clear only reached the alias's image; the image the game draws in kept the previous frame's second tile. fear-nx already mirrored two cases of this trick (stencil-only clears, zero-depth-as-colour clears), not this one.

**Fix** (`MirrorAliasClear` in `native_renderer.cpp`): after a clear-shaped draw (a rectangle, depth/stencil "always/replace", no blending, full colour mask), apply the same clear to every host image that aliases that EDRAM, with the rectangle mapped between sample layouts (fear-nx's `MapStencilClearRectangle`). The clear colour is computed by running the clear's vertex and pixel shaders in the SDK's shader interpreter (Direct3D passes it through an interpolator); depth from the rectangle's z. Checked at 1×, 2× and 3×: the intro text, credits and the "Interval 01 / Inception" card appear once and centred, no half frames, 3D scenes unchanged.

**Still open (no visible effect found):** 2× MSAA *depth* copy-outs are skipped by the renderer ("Depth resolve not done: 768x720, 2x MSAA"), so the scene-depth texture those frames write is never filled. Nothing in the intro samples it (traced every draw's textures); if an effect later in the game looks wrong, this is the place. Vulkan's depth resolve (`VK_RESOLVE_MODE_SAMPLE_ZERO_BIT`) into a single-sample image, then the existing depth copy, would fill it.

## PC textures: feasibility
Measured, not assumed ([TEXTURES.md](TEXTURES.md) has the full plan):
- **97% of the 360's texture names exist on PC** (6,069 of 6,251), even with the same folder paths.
- **84% of those are larger on PC** (5,105), almost all 2× per side (4× the pixels); 963 are the same size. The 360 halved its textures to fit in memory, and the PC has the originals.
- The 360 textures sit zlib-compressed in `uberCatalog.cat`, each with its Xenos fetch constant in the header; the PC ones are plain DDS in LithTech `LTAR` archives. Both are now readable (`tools/texture_compare.py`).
- Route: an offline tool builds a local pack keyed by a hash of each 360 texture's data, and fear-nx's texture cache, which already hashes texture data when it creates images, swaps in the PC DDS. No reverse engineering of the game's loader needed. Normal maps (360 DXN) need a format conversion; a downscale-and-compare quality gate catches pairs that aren't the same picture.

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

## Idea for later: engine overhauls, OpenCE-style (maybe in Rust)
Raised 2026-10-22: rewrite parts of the engine for speed, the way OpenCE did, possibly in Rust; partly in case the movement feel comes from how the game was squeezed onto the 360 rather than from movement code.
- **Where speed comes from here:** at 4K/120 the renderer mostly waits for the game, and the recompiled CPU code runs on a 9800X3D, far beyond a 360. There's no measured CPU bottleneck yet, so a rewrite for speed would be aimed at nothing. Profile first (the renderer already reports where frame time goes).
- **What OpenCE's rewrites actually bought** was *behaviour*: rendering between simulation ticks (Smooth Motion), lower input latency, configurable FOV. Those come from owning the code, not from the language. The recomp already allows the same thing one function at a time: replace a recompiled function with a hand-written one, as WK2D's N3 project does with tested C++ modules.
- **Language:** the SDK, the renderer and the generated code are C++; Rust would sit behind a C ABI at every boundary, for no speed gain (both compile through LLVM). Worth it only if a whole subsystem gets rewritten for maintainability.
- **For the movement feel specifically:** if it turns out to be console-era tuning or a 30 Hz assumption (head bob, camera smoothing, input filtering), the fix is a small hook on that function, measured against `-FrameRate 30` and against the PC version's values.

## Open items and next steps
1. **60 fps cap from outside the game:** check the NVIDIA driver/app *Max Frame Rate* and Afterburner/RTSS; then measure again (the 10-second report shows `present` time per frame).
2. **Right-stick ramp:** read `GPadAimEdgeAccelTime`, `YawAccel` & co., offer a setting without the ramp.
3. **Saving without Xbox Live:** a local signed-in profile the game accepts.
4. **Crash:** wait for a `crash-*.txt` from the new reporter.
5. **MSAA depth copy-outs** are skipped (see "Two-tile frames"); implement if an effect turns out to need them.
6. **Frame pacing:** test `-Present fifo` / `mailbox`; then lock the virtual vblank to the display (the real fix).
7. **Main menu Settings and Quit:** see [HANDOFF.md](HANDOFF.md#next-steps-the-users-list) for the two routes (host overlay vs in-game menu entry).
8. **Movement feel:** diff the 360 and PC game databases (bob, sway, acceleration); compare 30 vs 120 fps.
9. ***F.E.A.R. Files*:** dump the disc, find its executable(s), generate a second translation against the same runtime and renderer.
10. **Linux:** the renderer and SDK are Vulkan; port the build script.

The repository is public: <https://github.com/Juansolo457/fear360-recomp>.

## Sister projects
- `../n3-360-recomp`: N3: Ninety-Nine Nights on WK2D's ReXGlue recomp, parked at 4K / 40 fps.
- `../quake4-360-recomp`: Quake 4 Definitive Edition (openQ4 + the 360 controller feel).
