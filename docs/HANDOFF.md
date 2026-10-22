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
| Frame rate | locked to the display's refresh, at most 90 fps: 80 on a 240 Hz display, each frame shown for exactly 3 refreshes; `-FrameRate <n>` sets the limit, `-FrameRate 30 -Timer` is the console's pacing. Details in "Frame rate" below |
| Display | fullscreen at the desktop's 3840×2160; `--resolution 720p` tells the guest it is on a 720p display (fixes ReXGlue's 4:3 squeeze at 200% DPI) |
| Controls | SDL gamepads. Known trap: a Logitech virtual XInput pad (`046D:0000`) takes slot 0, making the Xbox pad player 2 ("No profile selected", no saves); disable that device |
| Stock renderer (`--nx_renderer xenos`) | still corrupts menus; kept only for A/B |

Commits: `a616a08` (port-nx + renderer), `e678aea` (resolution scaling), `4614a81` (launcher
`-FrameRate`), then 120 fps as the launcher default.

## Frame rate: what is known
- The game presents every second vblank interrupt (D3D `Present` interval 2), so its frame
  rate is the guest vblank rate over two: 30 fps at the console's 60 Hz. Game speed is
  right at any rate tried (variable timestep), but **above ~90 fps the 360's effect code
  breaks** (bullet tracers twitch and freeze mid-air: a per-frame step).
- **Pacing is locked to the display** (`port-nx/src/display_vblank.*`, default
  `fear_vblank_source=display`): the guest vblank follows the display's real vblank (DXGI
  `WaitForVBlank`), the guest refresh rate is set to the display's, and `native_system.cpp`
  `PaceToDisplay` holds each frame to exactly N refreshes (N = ceil(refresh /
  `fear_frame_rate_limit`), limit 90), presenting right after a vblank through mailbox. 80 fps
  on 240 Hz, 72 on 144, 60 on 60 (two guest vblanks per refresh). Falls back to the timer if
  the vblanks stop coming at the display's rate (variable refresh).
- `fear_vblank_source=timer` (launcher `-Timer`) is fear-nx's software timer at
  `video_mode_refresh_rate` (24–480 Hz with SDK patch 0003); it drifts against the display.
- The 10-second report's `pacing` line gives the time between presents and the display
  refreshes between them (from the real vblank in both modes).
- Past ~90 fps needs the tracer step fixed in the game code (decompile) or frame
  interpolation (OpenCE "Smooth Motion").

## How it is built and run
```
pwsh -NoProfile -File tools/build-nx.ps1            # SDK from source + codegen + game, ~15 min first time
pwsh -NoProfile -File tools/launch.ps1 -Nx -Extra '--nx_renderer native'   # defaults: -Scale 3 -FrameRate auto
pwsh -NoProfile -File tools/smoke.ps1 -Nx -Seconds 55 -Captures 3 -Extra '--nx_renderer native --nx_native_resolution_scale 3'
pwsh -NoProfile -File tools/capture_window.ps1 -ProcessName fear   # screenshot even when covered
& tools/autoplay.ps1 -Nx -Seconds 200 -CaptureEvery 2 -Tag x -Extra '--nx_renderer native --nx_native_trace_frame_every 120'
```
Renderer diagnostics: `--nx_native_trace_output` logs unique resolve/swap states;
`--nx_native_trace_frame_every N` logs everything one frame in N does (draws with targets,
masks, scissor, window offset and sampled textures; clear rectangles and colours; mirrored
clears; resolves; each draw's blend control as `bc=`). That trace found the tile-clear bug in
one run. With `--fear_texture_log_keys` the log also maps texture addresses to pack keys, which
`private/texwork/x360.tsv` names (`FX\Impacts\…`). The 10-second report also breaks the render
thread's frame down (`swap … (present …)`, `queue lock`, `vkQueueSubmit`) and gives a `pacing`
line. `autoplay.ps1 -KeysUntil 80 -ThenKey E` gets into the game, then keeps firing.
Game code: `--fear_dump_image private/image/default.bin` writes the decrypted executable image
(0x82000000) at the first frame, for reading constants and code next to the generated
`port-nx/generated/default/*.cpp` (each instruction is a comment there). The PC SDK 1.08 game
code and the LithTech runtime source are public and match the 360 code closely (ClientFX,
model animation); use them as a map, don't copy them in. A fix in the game code goes in as a
`[[entrypoint.midasm_hook]]` in `fear_manifest.toml` (inside the entrypoint table: a top-level `[[midasm_hook]]` is ignored without a word) calling our own function (see `src/anim_timing.*`).
Crashes write `port-nx/logs/crash-<time>.txt` + `.dmp` (`src/crash_report.cpp`): Windows Error
Reporting is off on the user's PC, so this is the only record.
- `third_party/rexglue-sdk-nx` (gitignored) = ReXGlue `c94f5eb` + fear-nx's
  `patches/native-vulkan/rexglue-sdk.patch` (tree `c7fb3c0f…`, verified) + our
  `patches/rexglue-sdk-nx/0001` (`REXGLUE_GPU_XENOS_STATIC`) and `0002` (achievement dialog
  created lazily, so no UI-thread painting holds the queue lock). Rebuild it with
  `git clone --no-checkout rexglue-sdk; checkout c94f5eb; git apply --index <fear-nx patch>;
  git am patches/rexglue-sdk-nx/*; git submodule update --init --recursive`.
- `tools/build-nx.ps1` already handles: VS DevShell (vcvars64 breaks on a quoted PATH entry),
  `-march=x86-64-v2` (SDK uses SSSE3 intrinsics), compiler-rt builtins linked by path
  (`__udivti3` from fear-nx's clock patch; `--rtlib` is ignored by the MSVC-target driver),
  `thirdparty/renderdoc` on the include path, codegen → reconfigure on the first build, and
  codegen again whenever `fear_manifest.toml` or the executable changes.
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
| `port-nx/src/fear_app.h` (`fear_shared_controllers`) | every controller drives player 1, the signed-in local profile that saves |
| `port-nx/src/look_tuning.*`, `tools/gamedb.py` | right-stick options (`fear_look_ramp`, `fear_aim_softlock`): a patched copy of `Loki.gamedb` served through a VFS redirect; reader for the 360's "BDAG" game databases |
| `port-nx/src/crash_report.*` | crash reports into `port-nx/logs/` |
| `port-nx/src/display_vblank.*` | the display's vblank (DXGI) and the pacing plan; used by `native_system.cpp` |
| `port-nx/src/anim_timing.*` | model animation time without the per-frame millisecond truncation; called from the generated code through `[[entrypoint.midasm_hook]]`s in `fear_manifest.toml` |
| `port-nx/src/fear_menu.*` | *PC Settings* and *Quit to Desktop* added to the game's own main menu: hooks in `CScreenMain::Build` and `OnCommand` that call the game's `AddTextItem`/`AddControl` (how to call guest functions from a hook: `current_ppc_context()`, save and restore the context, `ResolveIndirectFunction`) |
| `port-nx/src/fear_settings.*` | the settings panel and quit confirmation (ImGui, pad via XInput), `<user data>/settings.toml`, restart |
| `port-nx/src/fx_debug.*` | `fear_debug_model_fx`: logs ClientFX effects created (by type) and model effects' lifetime, color and scale |
| `port-files/` | *F.E.A.R. Files*: port-nx's host code with its own manifest and translation (`docs/FILES.md`) |
| `tools/find_missed_functions.py`, `tools/find_anim_sites.py`, `tools/xexinfo.py` | function entries the analyser missed (from a dumped image); this engine's animation-time sites in a translation; XEX headers |
| `port-nx/src/native/UPSTREAM.md` | upstream commit and the list of local changes; keep diffs small |
| `port-nx/fear_manifest.toml` | codegen manifest with fear-nx's 78 function hints (codegen rewrites its header; harmless) |
| `docs/FINDINGS.md` | disc/XEX facts, bug history, what was tried on the stock renderer |

## Next steps (the user's list)
**Current order (2026-10-22):** 1. Settings and Quit in the main menu: done ("looks awesome"; PROGRESS.md, "Main menu: PC Settings and Quit to Desktop"); 2. next: the *F.E.A.R. Files* campaigns at 4K with the texture pack ([FILES.md](FILES.md)); later, a side-by-side recording against the PC version with EchoPatch (PROGRESS.md, open item 13). The pistol's impacts are parked: they match a real 360 (open item 12).

Play-test 1 added: frame-pacing jank (`-Present` test, then lock the virtual vblank to the
display), chapter cards off-centre/doubled and cutscene-transition flicker (reproduced at 1×:
the renderer's two-tile frames; fixed 2026-10-22, see PROGRESS.md), PC texture replacement (done: a local pack,
`-TexturePack`, see [TEXTURES.md](TEXTURES.md)). Details in [PROGRESS.md](PROGRESS.md).

1. **Frame rate:** 120 fps via the vblank rate is the default and plays right. Before
   calling it done, check slow-mo duration, cutscenes and scripted timers against 30 fps,
   and add a frame-time counter (the renderer only logs frames over 50 ms) to see whether
   heavy scenes hold 120. A Lost Odyssey-style present-interval hook or interpolation only if
   something breaks or the user wants more than 120.
**After that, the user's idea (2026-10-22):** "one global launcher with all of the games launchable from one menu instead of having to jump different games". See [FILES.md](FILES.md#the-definitive-edition-one-game).

2. **Quit button in the main menu** (done the in-game way, see `fear_menu.*`; the notes below are the options as first weighed). Options, cheapest first:
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
