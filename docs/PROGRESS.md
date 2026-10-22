# Progress Debrief

_Last updated 2026-10-22 (play-test 2)._

## Where we are
F.E.A.R. for Xbox 360 is **playable natively on Windows at 4K and a measured, steady 120 fps**. Menus, the intro and menu videos, the campaign and Instant Action all work; game logic, gunplay and physics are the console's own and behave correctly. Hands-on after ~10 minutes: "guns responded very well… game logic is in there, the physics are in there". The resolution, AA and anisotropic filtering made "a huge difference. Very clean image".

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

## Play-test 2 (2026-10-22)
3× scale, PC texture pack. "The game looks much better. Everything looks, feels pretty great." Then four things.

**1. The game closed by itself** about 10 minutes in, ~10 s after a level load started. The log just stops; Windows recorded nothing because **Windows Error Reporting is disabled on this PC**. Added `src/crash_report.cpp`: an unhandled exception, the SDK's debug break (the game's `KeBugCheck` lands there), `abort()` and `exit()` (the game's `HalReturnToFirmware`) now leave `port-nx/logs/crash-<time>.txt` (exception, module+offset, symbolised stack; recompiled functions are named `sub_<360 address>`) and a small `.dmp`. Next crash will say where.

**2. Frame rate isn't 120, and isn't even.** Counters added to the 10-second report (`per frame, renderer thread: swap … (present …)`, `all submissions: queue lock …`) show where the render thread's time goes:
- GPU work is 2–5 ms per frame; texture work and GPU waits ~0. The thread spends **13–15 ms of every frame inside the present**.
- First layer: the SDK created an (idle) achievement-notification dialog at launch on Windows. Any registered UI dialog makes the presenter paint from the UI thread, whose present held the Vulkan queue lock, so our submits waited on that lock 13–15 ms. Fixed in `patches/rexglue-sdk-nx/0002` (create it on the first achievement, as fear-nx already does on Switch): queue lock wait now 0.00 ms.
- Second layer: with that gone, **`vkQueuePresentKHR` itself blocks 12–15 ms**, in immediate (no-vsync) mode on a 240 Hz display, with NVIDIA's Vulkan-over-DXGI layer on or off. Several 10-second windows show exactly 601 presents: a 60.1 fps limiter applied inside present, outside the game. RTSS/Afterburner is hooked (its OSD is in every capture) but its frame limit is off (`Limit=0`). Remaining suspects: an NVIDIA driver *Max Frame Rate* (global or per-app, or in the NVIDIA app), or an Afterburner/RTSS setting applied at runtime. To check on the user's side.
- Why it feels uneven rather than just "60": the game is paced by a 240 Hz virtual vblank and wants a frame every 2 vblanks (8.3 ms), but each present then stalls ~15 ms. Frames reach the screen at a steady 60 while the game's timing and input sampling run on a different rhythm.
- **Resolved:** a global *Max Frame Rate = 60* in the NVIDIA Control Panel on the test PC. With it off: **1,200 presents per 10 s (120 fps), present 0.13 ms per frame**. Lesson kept for the code: the game shouldn't depend on such settings. Two changes planned so no machine setting can do this again: present off the game's render thread (a held present then can't slow the simulation or input), and the frame rate / vsync as in-game settings.

**3. Right stick "seam":** "once you first start moving it… then you stop for a little bit… and move it again, you can feel almost like a slight delay." The 360 tuning has exactly the parts for that: in `Game/database/Loki.gamedb` **`GPadAimEdgeAccelTime` / `GPadAimEdgeMaxTurnRate`** (turn speed ramps up over time once the stick reaches its edge), **`TurningSoftLockYawFactor/PitchFactor`** and **`AimAssist.*`** (turning slows near targets); in the profile defaults `Game/ProfileDatabase/Defaults.gamedb` **`YawAccel` / `PitchAccel`**, **`Sensitivity`** and per-binding **`DeadZoneMin/Max`**. A ramp that restarts from zero each time the stick starts moving is the likeliest cause. It is 360 tuning, not a recomp bug; the PC version (mouse) has no such ramp.

**Read** (`tools/gamedb.py`, a reader for the 360's big-endian "BDAG" databases): `Client/Shared` has `GPadAimEdgeThreshold` 0.95, `GPadAimEdgeDelayTime` **0.3 s**, `GPadAimEdgeAccelTime` **0.4 s**, `GPadAimEdgeMaxTurnRate` 360°/s, `GPadAimAspectRatio` 0.5; `Client/SoftLock` has `TurningSoftLockYawFactor` **0.3** and `PitchFactor` 0.5. So: past 95% stick, the game waits 0.3 s, then ramps the turn rate up over 0.4 s to 360°/s, and every stop restarts the wait; near an enemy, turning drops to 30%.

**Option** (`port-nx/src/look_tuning.cpp`, launcher `-LookRamp`, `-NoSoftLock`): at startup a patched copy of the player's own `Loki.gamedb` goes to `<user>/overrides/Game/database/`, and the game's database folder is redirected to it (a VFS symbolic link to a read-only device that falls back to the original `Game` folder for everything else). Modes: `console` (as shipped), `nodelay` (ramp starts at once), `instant` (full boost at once), `off` (no boost: turn rate follows the stick). The log confirms the game read it ("the game reached the patched database folder").

**Verdict (hands-on, `off`):** "That fixed the stick issue for sure… it feels significantly better without that ramp up… The feel is 100% much better." **`off` is now the default** (cvar and launcher). The same session still presented at exactly 60 fps (59.7 average, 60.1 max; present blocking 13–14 ms), so "the frame rate felt better" was the stick, not the cap.

**4. Saving.** The game said it would play without a profile. Same cause as N3: the runtime signs in only player 1 (a local profile, no Live needed), and SDL numbers controllers in connection order: a Logitech XInput device (`046D`) was slot 0, the Xbox Series pad slot 1, so the pad was player 2, not signed in. Proof: the keyboard-driven test runs (always player 1) have `checkpoint.sav` files; the hands-on profile has none. **Fix:** `fear_shared_controllers` (default on) installs the SDK's `SharedAssignment` in the app's input factory: every controller and the keyboard drive player 1. Needs a hands-on check that a checkpoint save appears under `port-nx/user/`.

**Also asked for:** the Options and Quit entries in the main menu.

## Play-test 3 (2026-10-22)
4K, true 120 fps for the first time (the driver cap off), PC textures, no stick ramp. 8½ minutes, a steady 120.1 fps average, lowest 10-second window 113 fps, no slow frames, no crash. "With the sticks, it just felt really good."
- **Bullet tracers glitch in heavy firefights:** "multiple enemies would be shooting bullets and it would just start basically corrupting midair… the traces of the bullets would just start… twitching in midair and be stuck there for a second", unlike the PC version where every tracer flies cleanly. The user suspects the frame rate is more than the 360 code handles. **The launcher default is now 90 fps** (`-FrameRate 90`, a 180 Hz virtual vblank) to compare; next, compare 60/90/120 in the same fight. If it scales with frame rate it's a per-frame step in the effect code (tracer update, particle lifetime); if not, it's the renderer (dynamic vertex data).
- **At 90 fps the tracers are fine:** "Yep, the tracers is all gone… At 90 frames, it feels really good." So the glitch scales with frame rate: the 360's effect code takes per-frame steps that break down past ~90. 90 stays the default; making 120 safe means finding that step (a decompile target).
- **White flicker, "on load-in when I'm looking at a new map… then it kind of disappears."** Captured on video (NVIDIA instant replay, 60 fps). Brightness per frame shows **one frame at 4.530 s** (mean 23.5 → 31.7 → 22.3) holding a bright soft-edged cone over the right of the scene and a dark see-through rectangle near the top centre: light/fog effects drawn without one of their textures. **Cause:** fear-nx's renderer (built for Switch, where creating a texture costs ~1.5 ms) streams new textures in on worker threads and draws with the empty (black) texture until they arrive; their own note says it "shows as a flash of the empty texture". Effects that use a texture as a mask flash white or black for that frame. **Fix:** on PC the draw now waits for a streaming texture (`fear_texture_stream_wait_ms`, 100 ms per frame at most, then they pop in as before), with 6 workers instead of 2. Measured through the intro at 3×/90 fps: every streamed texture waited for, **0 drawn without** (was 15–80 per 10 s), at most 158 ms of waiting per 10 s in the biggest load-in (211 textures), 89–90 fps held, no texture-caused slow frames.
- **Saving still unconfirmed:** nothing was written to `port-nx/user/` in 8½ minutes, while a keyboard test run with both pads connected (after the controller fix) saved its checkpoint. Which mode was played (campaign or Instant Action, which may not checkpoint) is the next question.

## Renderer sweep for PC (2026-10-22)
Asked after play-test 3: what in fear-nx's renderer is a Switch tradeoff that a PC doesn't need, and would a rewrite (Rust) make it faster?

**Where the time goes now** (play-test 3, 4K, 90 fps): per frame the renderer uses **0.67 ms of CPU and 1.6 ms of GPU** (6.4 ms at worst), and waits ~10 ms for the game. It is idle ~90% of the time. Making it faster changes nothing on screen; a rewrite, in Rust or otherwise, would buy no frames. What decides smoothness now is *when* frames happen and what goes missing, not how fast they're drawn.

**Switch tradeoffs found:**
| | On Switch | Effect on PC | Status |
|---|---|---|---|
| New textures stream in, drawn with the empty texture until ready | creating a texture costs ~1.5 ms | the white flash at load-ins | **fixed** (wait, 6 workers) |
| UI-thread painting from an idle dialog | halved frames there too | 14 ms queue-lock stall per frame | **fixed** (SDK patch 0002) |
| **Draws skipped while their shader is translated or pipeline compiled** (`nx_native_async_pipelines`) | a compile stalls a frame for long | **2,500–2,800 draws skipped per session**, in a few load-in windows: objects and effects missing for a few frames the first time they're seen | **fixed**: draws wait (`fear_shader_wait_ms`, 100 ms per frame at most). Intro at 3×/90 fps: **0 draws skipped**; 205 waits (56 ms in all) at startup, then 0–26 per 10 s (0–7 ms), 90 fps held. A disk cache / pre-build turned out unnecessary: pipelines already come from the persistent driver cache, and translation costs ~0.3 ms a shader |

**Two fear-nx bugs found on the way** (with the Vulkan validation layer, LunarG SDK 1.4.363): turning the waits on lost the device on the first frames at 3× in 6 of 8 runs.
- *The cause:* the texture cache keeps its own frame number and started at 0 while the renderer starts at 1. Upload buffers retired during frame 1 were tagged frame 0, counted as finished at the first swap and freed while the GPU still copied from them (`VUID-vkDestroyBuffer-buffer-00922`, then `VK_ERROR_DEVICE_LOST`). Waiting made textures finish uploading in frame 1; 4K made the GPU still be reading. Fixed: the cache is synced to the renderer's frame at startup.
- *Also:* the constant-buffer descriptors (system, float, bool/loop, fetch constants) were written in one update across bindings with different stage flags, which Vulkan forbids; validation reported bindings 1–4 as never written. Fixed: one write per binding.
- Left: `VUID-vkQueueSubmit-pSignalSemaphores-00067` in the SDK's window presenter (a swapchain semaphore reused); harmless so far.
| **New textures streamed in one at a time** (each draw waits for its texture's job before the next draw asks for one) | creating an image costs ~1.5 ms | a 300-texture load-in waited 112 ms for ~20 ms of work (6 workers, 5 idle); frames over 100 ms and the wait budget blown | **fixed**: every new texture is created on the spot and filled (untiled, or read from the pack) by **6 upload workers** before the frame is submitted (`fear_texture_upload_direct`). Intro at 3×/90 fps: **no frame over 50 ms after the first load at boot** (was 1–2 per load-in, some over 100 ms), upload wait 0.00 ms per frame, 90 fps in every window |
| Thread priorities, driver-cache save throttling, small per-frame create budgets | SD card, 3 cores | harmless | leave |

**Frame pacing:** the game's frames come from a software vblank timer, not the display. At 90 fps on a 240 Hz display a frame stays up for 2 or 3 refreshes alternately (8.3/12.5 ms): visible judder. **Hands-on, 80 fps: "80 feels great, I notice the feel is better than 90"** (exactly 3 refreshes each). The launcher now defaults to `-FrameRate auto`: the highest rate up to 90 that divides the display's refresh (80 on 240 Hz, 72 on 144, 82.5 on 165, 60 on 60/120). 240 fps was tried (SDK patch 0003 allows a 480 Hz guest vblank; 163–234 fps measured at 4K, render-thread bound in heavy scenes): the tracers break, as at 120. **Then locked to the display** (play-test 4): the game's vblank now follows the display's real one, and every frame in play was shown for exactly 3 refreshes.

## What's left
**For a definitive port** (roughly where the "80%" sits):
1. **Saving:** the fix works (the profile is written during play), but play-test 3 was Instant Action, which doesn't checkpoint; one campaign checkpoint to confirm.
2. **Options and Quit** in the main menu.
3. **No pop-in on first sight** (the shader/pipeline item above).
4. **Even pacing: done** (display-locked, play-test 4). Left: **120 fps without the tracer glitch** (the per-frame step in the game's effect code), and any other per-frame step like the animation one fixed in play-test 4.
5. **A level sweep:** every Interval, the bonus level and the Instant Action maps, played or driven, for crashes and visual faults; the crash reporter catches anything.
6. **Linux** build; ***F.E.A.R. Files***.

**For a decompile** (the long project, OpenCE-style): rewriting the game's code as readable source, subsystem by subsystem, each checked against the recompiled original. It's not needed for any item above, and it's where 120+ fps behaviour (simulation/render decoupling, the tracer step) and deeper features come from. The PC version stays the reference for how each system behaves.

## Movement feel: 360 vs PC
The user's sense: walking feels a little more bouncy than on PC, maybe extra sway. Two ways it could be real, both checkable:
- **Different tuning on 360.** Console ports often retuned head bob, weapon sway and look acceleration for a pad. Both versions ship their tuning as a LithTech game database (360: `Game/database`, `database_anim`; PC: `FEAR*.Gamdb00p` inside `FEAR.Arch00`). Diff the player/camera/weapon records (head bob, sway, movement acceleration, look sensitivity) between them. If they differ, the 360's values are what the 360 shipped, and a setting could offer the PC's.
- **Frame-rate dependence.** The game was tuned at 30 fps; if bob/sway or camera smoothing is integrated per frame, 120 fps can change its amplitude or feel. Compare `-FrameRate 30` against `120` in the same spot. If the 30 fps run feels like PC and 120 doesn't, it's ours to fix (scale the per-frame step).
- A measured comparison beats feel: record the camera position/orientation per frame (a guest-memory read of the player camera, or screen captures while walking in a straight line) at 30 and 120 fps, and on PC with EchoPatch at 120.

## Play-test 4 (2026-10-22, display-locked pacing)
4K, 80 fps, PC textures, Instant Action, ~10 minutes. The verdict: "that playtest is really good. Controls are feeling really, really good. Frame rate's feeling really good."

**What changed.** The game paces itself on the vblank interrupt and presents every second one. fear-nx raised that interrupt from a software timer on its own clock, so even at a rate that divides the display's refresh (80 on 240 Hz) the two drift apart and now and then a frame lands a refresh early or late. Now (`port-nx/src/display_vblank.*`, `fear_vblank_source=display`, the default) the interrupt follows the display's real vblank, from DXGI's `WaitForVBlank` on the primary output (any GPU vendor). Each frame is held to exactly N refreshes, the fewest that keep it at or under `fear_frame_rate_limit` (90): 3 on 240 Hz, 2 on 144 Hz, 1 on 60 Hz with two guest vblanks per refresh. It goes out right after the vblank that ends its N, through mailbox presentation (immediate would tear it a little way down the screen), so it is shown at the next refresh. A display whose vblanks stop coming at its rate (variable refresh, switched off) falls back to the timer; G-SYNC is off on the test PC and it never fell back. `-Timer` in the launcher keeps the old timer.

**Measured** (a new `pacing` line in the 10-second report: time between presents, and the display refreshes between them, counted from the real vblank in both modes):

| | frames 3 refreshes apart | spread of the frame time |
|---|---|---|
| timer, 80 fps (windowed autoplay, 30 s in game) | 800 of 800 per 10 s | 0.13 ms |
| display, 80 fps (this play-test, ~9 minutes in game) | **800 of 800 in every 10 s window** | **0.02 ms** |

The timer did well in its 30 s too: its drift shows up rarely, as a frame a refresh early or late when the phase crosses a refresh. The display lock removes that by construction. Four frames of 210–245 ms showed up in one menu stretch: the game itself busy (the ring idle, waiting for it), not the renderer.

**New: dark "rocks" and a haze where bullets hit walls.** First seen as a haze in the rooftop steam; the user then pinned it down: "if you shoot into the wall… and then walk past the holes… you'll see that weird haze effect again". In the recording the impact puffs are big, dark, nearly opaque shapes sticking out of the wall, still there over a second later, and walking into one puts a dark round haze over the screen. Not the texture pack (same without it), not the renderer (the draws' blend, depth and constants are what the game sets).

**At 30 fps it's right:** "the old bug is gone… it definitely looks like the smoke sticks around for a little bit… walking through it… just looks like you're going through smoke now". So the game's own code breaks at high frame rates, like the tracers above 90.

**Found and fixed on the way: animation time in whole milliseconds.** The engine steps model animations in whole milliseconds (LithTech's `trk_Update`): the frame time becomes `msDelta = (uint32)(seconds * 1000)` and each tracker steps `(uint32)(msDelta * rate)`. Both drop the fraction: every animation runs 1% slow at 30 fps and 4% at 80, and one slowed below 1/12 of normal speed freezes outright at 80 fps (12 ms x rate < 1). In the 360 code (matched against the SDK and LithTech sources; image dumped with `--fear_dump_image`): `trk_Update` is `sub_82191E40`, the model update computing `msDelta` `sub_822415D0` and `sub_821E4DA8`. Fix (`port-nx/src/anim_timing.*`, `fear_anim_exact_time`, on by default): three `[[entrypoint.midasm_hook]]`s keep the dropped fraction per model and per tracker and add it back, so animations advance by exactly the time that passed.

**The rocks are F.E.A.R.'s own bullet holes.** With the animation fix: "the smoke puff test looked a lot better, but still saw that same weird artifact". Full-resolution crops of that clip show the rocks are flat on the wall (one wraps a corner the way a projected decal does) and carry the stone bullet-hole textures (`BH_Stone2_D`, `BH_Stone3_D`, `BH_Stone4_D`: a crater with a chipped rim, drawn alpha-tested and bump-mapped, hence the hard edge and the 3D look). A log in the 360's `CDecalFX::ProjectDecal` (`sub_82B98960`, found through the Decal type's method table) gives each decal's size: width and height are a random draw between the effect's `ScaleMin` and `ScaleMax`, set once when it's placed, with no frame time involved. A scripted campaign run that shoots walls gives the same two families at 80 and at 30 fps (5-20 units: 135 and 70 decals; 30-50 units: 43 and 31). The big ones are in the data, as on the console.

**The haze** (a flat grey wash over the screen while passing impacts, with a dark round soft edge crossing it) looks like the impact smoke's soft cloud particles seen from inside. It hasn't come back in the user's two runs since (10:30, 11:03: "no haze effect"). Logs on the way: the 3D puff models (`LTBModel`) didn't spawn at all in those Rooftop runs (the effect-creation log: 1,524 effects, by type); where they do spawn (25 in a campaign run) their life advances smoothly at 80 fps. Every ClientFX update read so far (particles, models, decals, overlays) is time-based. `fear_debug_model_fx` stays in (off by default) for the next sighting.

Ruled out on the way: EchoPatch's PC ClientFX fixes ("Frozen FX effects", "Oversized particles": a 1 ms particle-update skip and batch markers with lifetime -1). The 360 build has the same marker code (`sub_82BB65E0`, `sub_82BB6E40`) but dropped the 1 ms skip, so its markers never reach the screen.

## Frame pacing: diagnosis
- The log shows the swapchain in **presentation mode 0 = IMMEDIATE**: no vsync and no triple buffering. The SDK prefers it for latency (`vulkan_presenter.cpp`).
- The game's frames are paced by fear-nx's **software vblank thread** (`VblankLoop`, a host timer at `video_mode_refresh_rate`), which is **not locked to the monitor's real refresh**. Presented immediately, frames reach the 240 Hz display at slightly uneven intervals and can tear. That reads as occasional jank even at a high average. Windowed-fullscreen Vulkan on Windows also goes through the compositor, so G-SYNC/VRR is unlikely to be smoothing it.
- **Test now:** `tools/launch.ps1 … -Present fifo` (true vsync: even, slightly more latency) and `-Present mailbox` (no tearing, low latency, still unsynced).
- **Done** (see play-test 4). The plan was: drive the virtual vblank from the display's actual vblank (e.g. DXGI `IDXGIOutput::WaitForVBlank` on Windows) instead of a timer, so the game's "every 2 vblanks" lands exactly on every 2nd refresh at 240 Hz. Or present to an exclusive/independent-flip swapchain so VRR engages.

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

## Direction for later: decompile and rewrite, OpenCE-style
Raised 2026-10-22: rewrite parts of the engine for speed, the way OpenCE did, possibly in Rust; partly in case the movement feel comes from how the game was squeezed onto the 360 rather than from movement code.
- **Where speed comes from here:** at 4K/120 the renderer mostly waits for the game, and the recompiled CPU code runs on a 9800X3D, far beyond a 360. There's no measured CPU bottleneck yet, so a rewrite for speed would be aimed at nothing. Profile first (the renderer already reports where frame time goes).
- **What OpenCE's rewrites actually bought** was *behaviour*: rendering between simulation ticks (Smooth Motion), lower input latency, configurable FOV. Those come from owning the code, not from the language. The recomp already allows the same thing one function at a time: replace a recompiled function with a hand-written one, as WK2D's N3 project does with tested C++ modules.
- **Language:** the SDK, the renderer and the generated code are C++; Rust would sit behind a C ABI at every boundary, for no speed gain (both compile through LLVM). Worth it only if a whole subsystem gets rewritten for maintainability.
- **For the movement feel specifically:** if it turns out to be console-era tuning or a 30 Hz assumption (head bob, camera smoothing, input filtering), the fix is a small hook on that function, measured against `-FrameRate 30` and against the PC version's values. (The right-stick "seam" turned out to be exactly that kind of tuning: data, fixed without touching code.)

**Direction set after play-test 2:** "what we're looking at now is a full decomp of the 360 version… like OpenCE, where they basically did rewrites, and we can do that section by section. The game doesn't have a lot of variety, so I think it would be doable." How that maps onto the recomp:
- **The recomp is the decomp's test harness.** Every 360 function already runs as generated C++ (`sub_<address>`). A section is decompiled by replacing those functions with hand-written code and checking it against the generated original on the same inputs (the approach WK2D uses for N3's tested native modules), so the game stays playable at every step.
- **Naming the code:** the XEX has no symbols. The F.E.A.R. Public Tools SDK ships the PC game-side source (client/server game code, menus, HUD, AI) and the 360 game code is a port of the same codebase, so its strings and structure can name most game-side functions. That source is under its EULA: use it as a map, don't copy it into this public repository. The LithTech engine itself is closed on both versions and has to be read from the PPC code.
- **Order by payoff:** the frame loop and presentation first (render between simulation ticks for smooth high frame rates, input latency), then input/camera, then the systems where rewrites buy features (menus: Options/Quit; saves), the rest only as needed.

## Open items and next steps
1. **Robust presenting:** present off the game's render thread, so a driver cap, an overlay or vsync can't slow the game itself; frame rate and vsync as in-game settings.
2. **Load-in hitches: fixed** (direct uploads on 6 workers, see the sweep). Left: three frames of 50–100 ms at the first load at boot (66 pipelines, 75 shaders, 230 textures at once, behind the legal screens).
3. **Recorded glitch sweep (user idea):** 2–3 minutes of raw play from boot into the game, recorded; go through it frame by frame (`tools/flicker_scan.py` and by eye) and fix what shows up.
4. **Saving:** confirm a checkpoint save appears in `port-nx/user/` with the pad now on player 1 (the first session after the fix was too short to reach one).
5. **Crash:** wait for a `crash-*.txt` from the new reporter.
6. **MSAA depth copy-outs** are skipped (see "Two-tile frames"); implement if an effect turns out to need them.
7. **Frame pacing: done** (display-locked vblank, play-test 4). The rocks at bullet impacts are the game's own bullet-hole decals (same sizes at 30 fps); the haze hasn't come back since play-test 4 (watching, with `fear_debug_model_fx`).
8. **Main menu Settings and Quit:** see [HANDOFF.md](HANDOFF.md#next-steps-the-users-list) for the two routes (host overlay vs in-game menu entry).
9. **Movement feel:** diff the 360 and PC game databases (bob, sway, acceleration); compare 30 vs 120 fps.
10. ***F.E.A.R. Files*:** dump the disc, find its executable(s), generate a second translation against the same runtime and renderer.
11. **Linux:** the renderer and SDK are Vulkan; port the build script.

The repository is public: <https://github.com/Juansolo457/fear360-recomp>.

## Sister projects
- `../n3-360-recomp`: N3: Ninety-Nine Nights on WK2D's ReXGlue recomp, parked at 4K / 40 fps.
- `../quake4-360-recomp`: Quake 4 Definitive Edition (openQ4 + the 360 controller feel).
