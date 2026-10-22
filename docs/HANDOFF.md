# Handoff: F.E.A.R. 360 on Windows (2026-10-21, end of day)

Read [PROGRESS.md](PROGRESS.md) first: status, the first play-test's findings, the frame-pacing
diagnosis, the PC-texture feasibility and the recomp-vs-PC decision. [FINDINGS.md](FINDINGS.md)
has the disc/XEX facts and the history of the menu bug. This file is the build/run detail and
where things live.

## Where it stands
**The game is playable on Windows at 4K.** `port-nx/` builds fear-nx's patched ReXGlue SDK from
source with Vulkan, links the Xenos GPU plugin into `fear.exe`, and compiles fear-nx's Vulkan
Xenos renderer (`port-nx/src/native/`, GPL-3.0) with our integer resolution scaling added. With
`--nx_renderer native` the title screen, menu videos, text, HUD and gameplay all render
correctly. The user played Instant Action and the campaign start: logic, firing and physics
are fine. At 3× (3840×2160 internal) the image is clean and the RTX 4090 is far from busy.

| | |
|---|---|
| Resolution | `--nx_native_resolution_scale 1..4` (our change; `tools/launch.ps1 -Nx -Scale 3`) |
| Anti-aliasing | the game's own 2×/4× MSAA, honoured by the renderer (`nx_native_msaa`), on top of the scale |
| Anisotropic filtering | up to 16× by default (`nx_native_max_anisotropy`) |
| Frame rate | the game presents every 2 virtual vblanks → 30 fps at the console's 60 Hz vblank. **The launcher now defaults to `-FrameRate 120` (`--video_mode_refresh_rate 240`, the SDK's maximum) → up to 120 fps game frames; the user reports it smooth with game speed correct.** `-FrameRate 60` (120 Hz vblank) was verified the same way; `-FrameRate 30` restores console pacing. Details in "Frame rate" below |
| Display | fullscreen at the desktop's 3840×2160; `--resolution 720p` tells the guest it is on a 720p display (fixes ReXGlue's 4:3 squeeze at 200% DPI) |
| Controls | SDL gamepads. Known trap: a Logitech virtual XInput pad (`046D:0000`) takes slot 0, making the Xbox pad player 2 ("No profile selected", no saves); disable that device |
| Stock renderer (`--nx_renderer xenos`) | still corrupts menus; kept only for A/B |

Commits: `a616a08` (port-nx + renderer), `e678aea` (resolution scaling), `4614a81` (launcher
`-FrameRate`), then 120 fps as the launcher default.

## Frame rate: what is known
- Nothing we changed touches timing. The renderer's log shows it idling ~46 of every 50 ms
  "waiting for the game": the 30 fps is the game's own cap, as on the console.
- The native renderer's vblank thread (`native_system.cpp` `VblankLoop`) ticks at
  `VdQueryVideoMode().refresh_rate` = SDK cvar `video_mode_refresh_rate` (default 60, clamp
  24–240). The game's D3D `Present(interval 2)` waits for 2 of those interrupts. Raising the
  cvar is the zero-code way to raise the game's frame rate. The PC version of this engine
  (LithTech Jupiter EX) runs uncapped with a variable timestep, so the game logic should cope
  better than N3's fixed 40 Hz tick did, but that is the thing to verify (animation speed,
  slow-mo duration, physics, scripted timers).
- **Results (2026-10-21), 3× scale, user play sessions:** 120 Hz vblank (`-FrameRate 60`) and
  240 Hz vblank (`-FrameRate 120`) both felt smooth with game speed right. 120 is now the
  launcher default. Not yet checked: slow-mo duration, cutscenes, scripted timers, late
  levels, and whether heavy scenes actually hold 120 (the renderer doesn't log an fps; its
  "slow frames" counters only flag frames over 50 ms). The engine's variable timestep seems
  to carry it, unlike N3; keep an eye out for frame-counted logic as Lost Odyssey found.
- **240 Hz is the ceiling of this trick:** `ConfiguredVideoModeRefreshRate` clamps
  `video_mode_refresh_rate` to 24–240. Above 120 fps would need the interval-2 present
  released at its call site (Lost Odyssey style) or interpolation.
- Host present is vsynced (`vsync` cvar) to the 240 Hz display. fear-nx's own pacing cvars
  (`nx_min_frame_vblanks`, `nx_pace_before_present`) only add holds; leave them at defaults.
- If the vblank trick misbehaves, the Lost Odyssey approach is the model: find the present
  call site, release its interval there, pace on the host, and fix per-frame logic one case
  at a time (`../n3-360-recomp/reference/LostOdysseyRecomp/docs/notes/native-90-120fps.md`).
- A 240 Hz display shows a 30 fps game frame for 8 refreshes, a 60 fps one for 4 and a
  120 fps one for 2. Frame interpolation (OpenCE "Smooth Motion") would be the route past
  the game's own limits.

## How it is built and run
```
pwsh -NoProfile -File tools/build-nx.ps1            # SDK from source + codegen + game, ~15 min first time
pwsh -NoProfile -File tools/launch.ps1 -Nx -Extra '--nx_renderer native'   # defaults: -Scale 3 -FrameRate 120
pwsh -NoProfile -File tools/smoke.ps1 -Nx -Seconds 55 -Captures 3 -Extra '--nx_renderer native --nx_native_resolution_scale 3'
pwsh -NoProfile -File tools/capture_window.ps1 -ProcessName fear   # screenshot even when covered
& tools/autoplay.ps1 -Nx -Seconds 200 -CaptureEvery 2 -Tag x -Extra '--nx_renderer native --nx_native_trace_frame_every 120'
```
Renderer diagnostics: `--nx_native_trace_output` logs unique resolve/swap states;
`--nx_native_trace_frame_every N` logs everything one frame in N does (draws with targets,
masks, scissor, window offset and sampled textures; clear rectangles and colours; mirrored
clears; resolves). That trace found the tile-clear bug in one run.
- `third_party/rexglue-sdk-nx` (gitignored) = ReXGlue `c94f5eb` + fear-nx's
  `patches/native-vulkan/rexglue-sdk.patch` (tree `c7fb3c0f…`, verified) + our
  `patches/rexglue-sdk-nx/0001` (`REXGLUE_GPU_XENOS_STATIC`). Rebuild it with
  `git clone --no-checkout rexglue-sdk; checkout c94f5eb; git apply --index <fear-nx patch>;
  git am patches/rexglue-sdk-nx/*; git submodule update --init --recursive`.
- `tools/build-nx.ps1` already handles: VS DevShell (vcvars64 breaks on a quoted PATH entry),
  `-march=x86-64-v2` (SDK uses SSSE3 intrinsics), compiler-rt builtins linked by path
  (`__udivti3` from fear-nx's clock patch; `--rtlib` is ignored by the MSVC-target driver),
  `thirdparty/renderdoc` on the include path, codegen → reconfigure on the first build.
- `port-nx/assets` is a junction to `port/assets` (3.8 GB extracted disc; gitignored).
- `port/` is the stock-SDK build (nightly `bd833a2`, D3D12 only). Keep for comparison.
- The smoke tool opens a window on the user's screen; the user has played in it by mistake.
  Say so before running it. **Never close the user's game**; ask them to quit.

## Where things live
| | |
|---|---|
| `port-nx/src/fear_app.h` | app glue: picks fear-nx's renderer or calls the statically linked `rex_gpu_create` (`--fear_gpu_backend any/d3d12/vulkan`) |
| `port-nx/src/native/native_system.cpp` | fear-nx's graphics system: vblank thread, MMIO/ring, pacing, present (`Swap`) |
| `port-nx/src/native/native_renderer.cpp` | the Vulkan Xenos renderer (12.8k lines with textures): render targets, draws, resolves, swap. Our scaling: `Impl::scale`, `host_width/height`, `hx0/hy0/hwidth/hheight` in `Resolve()` |
| `port-nx/src/native/native_textures.cpp` | texture cache; resolve destination images (scaled by `resolution_scale_`), `last_resolved()` |
| `port-nx/src/texture_pack.*`, `port-nx/tools/texindex.cpp`, `tools/texture_pack.py` | PC texture pack: runtime lookup/streaming, offline key indexer, pack builder ([TEXTURES.md](TEXTURES.md)) |
| `tools/autoplay.ps1`, `tools/ab_pairs.py` | unattended runs (taps A, screenshots) and A/B frame pairing |
| `port-nx/src/native/UPSTREAM.md` | upstream commit and the list of local changes; keep diffs small |
| `port-nx/fear_manifest.toml` | codegen manifest with fear-nx's 78 function hints (codegen rewrites its header; harmless) |
| `docs/FINDINGS.md` | disc/XEX facts, bug history, what was tried on the stock renderer |

## Next steps (the user's list)
Play-test 1 added: frame-pacing jank (`-Present` test, then lock the virtual vblank to the
display), chapter cards off-centre/doubled and cutscene-transition flicker (reproduced at 1×:
the renderer's two-tile frames; fixed 2026-10-22, see PROGRESS.md), PC texture replacement (done: a local pack,
`-TexturePack`, see [TEXTURES.md](TEXTURES.md)). Details in [PROGRESS.md](PROGRESS.md).

1. **Frame rate:** 120 fps via the vblank rate is the default and plays right. Before
   calling it done, check slow-mo duration, cutscenes and scripted timers against 30 fps,
   and add a frame-time counter (the renderer only logs frames over 50 ms) to see whether
   heavy scenes hold 120. A Lost Odyssey-style present-interval hook or interpolation only if
   something breaks or the user wants more than 120.
2. **Quit button in the main menu.** Options, cheapest first:
   - Host-side: ReXGlue's ImGui overlay. `rex::ReXApp::OnCreateDialogs(ui::ImGuiDrawer*)` is
     the hook; the SDK already has a debug overlay and a cvar settings overlay (WK2D's N3
     project mentions changing cvars "through the SDK overlay"). A keyboard/controller
     shortcut that opens a small dialog with Quit (and later Options) needs no game patching.
   - In-game: the way WK2D added a "Graphics" entry to N3's main menu: derive a modified
     menu layout from the game's own UI data at first run (never ship it) and hook the
     menu's action handler for the new entry. For F.E.A.R. the UI is LithTech's (`Game/`
     database and `.cat` catalogues); more reverse engineering, but it looks native.
3. **Options menu** (resolution scale, AA, AF, frame rate, controls): same two routes.
   Settings persist via ReXGlue's TOML config (`<user_data_root>/…toml`, see how WK2D's
   `profile_config.cpp` writes typed TOML atomically).
4. **Later:** FXAA/SMAA post-AA (MSAA + 3× is already heavy supersampling), ultrawide/HUD
   scaling, Linux build (the SDK and this renderer are Vulkan; the Windows-only bits are the
   DevShell/MSVC CRT details in `build-nx.ps1` and the presenter), save/profile handling for
   whichever pad is slot 0, and upstreaming the scaling to fear-nx (GPL, so publishable).

## Licensing reminders
`port-nx/src/native` is GPL-3.0-or-later (fear-nx ← Project8Recomp ← nfsmw-nx); builds that
link it are GPL. ReXGlue is BSD-3-Clause with Xenia notices. The MIT license covers our own
files only; see `NOTICE`. Never commit game data, generated translations, captures or saves.
