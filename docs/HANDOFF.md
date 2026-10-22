# Handoff: F.E.A.R. 360 on Windows (paused 2026-10-21)

Start with [FINDINGS.md](FINDINGS.md) for the disc/XEX facts, then this file.

## Where it stands
- `port/` (a `rexglue init` project + fear-nx's 78 function hints) **builds and runs** on Windows (D3D12, RTX 4090): the intro, Press Start, the main menu and its submenus all work, input works, audio works, and **gameplay renders correctly**. The user played an Instant Action map with a clean image: level, lighting, weapon and HUD.
- **Still broken: menus, videos and some text.** Behind the menus the screen shows the game's font + button-glyph sheet stretched full-screen. Menu strings sometimes draw with the **right positions but wrong glyphs**. Intro and menu videos (Bink, CPU-decoded; audio plays) never appear. It flickers, and the user once saw the menu fully correct for about 5 seconds. Pattern: anything the game **writes into a texture from the CPU each frame** (glyph caches, Bink frames), or anything it renders to a texture and samples, shows stale or wrong texture contents. 3D world textures, which are static, are fine.

## Tried (no effect on the menu bug unless noted)
All runs use `tools/smoke.ps1 -Captures 4 -Extra '<flags>'`; screenshots are in `private/captures/`.

| Flags | Result |
|---|---|
| default (`resolution_scale` 3 or 1) | Font sheet behind the menu, wrong glyphs. Not caused by resolution scaling |
| `--no-clear_memory_page_state` | Same |
| `--no-direct_host_resolve` | **The background sometimes shows through** (flickers), per the user; the best of the bunch |
| `+ --render_target_path_d3d12 rov` | Same flicker / wrong glyphs |
| `+ --readback_resolve full` | Same |
| `+ --no-async_shader_compilation` | Black frames, then the font sheet mapped onto **3D geometry** |
| `--no-gpu_3d_to_2d_texture` (± `--no-direct_host_resolve`) | Menus still broken. Gameplay was clean in this run, but untested without it |

Not the cause: missing files (the failed opens are optional probes), the GPU plugin (fixed by `config.gpu_plugin = "xenos"`), or the pipeline count (pipelines load from cache).

## Leading theory
The texture cache in ReXGlue's Xenos plugin (Xenia-derived, D3D12) doesn't invalidate or rebind textures whose memory the CPU or a resolve rewrites, or it confuses textures that share memory or keys (font pages / video planes / render-to-texture). Xenia Canary rates this game **playable on D3D12** ([issue 628](https://github.com/xenia-canary/game-compatibility/issues/628)), so this is a ReXGlue difference or regression rather than an unknown hardware feature.

## Next steps, cheapest first
1. **Baseline in Xenia Canary** (ask the user before downloading it). If the menus are clean there, diff ReXGlue `bd833a2` against Xenia Canary in `src/graphics/pipeline/texture/cache.cpp`, `shared_memory.cpp`, `d3d12/texture_cache.cpp` and the resolve path. Watch for ReXGlue-only features: `direct_host_resolve`, `gpu_3d_to_2d_texture`, `clear_memory_page_state` (defaults to **true** here, false in Xenia).
2. **Trace one broken draw:** log the fetch constants (base address, format, size, dimension) of draws during the Press Start screen and check whether the full-screen quad's texture address matches the font texture's. That separates "stale contents" from "wrong binding".
3. **Port fear-nx's renderer** (GPL-3.0; Vulkan; written for exactly these problems: shared resolve aliases, tiled-resolve invalidation, video planes). The groundwork is done:
   - `third_party/rexglue-sdk-nx/`: ReXGlue `c94f5eb` + `fear-nx/patches/native-vulkan/rexglue-sdk.patch` (sha256 `9db89714…`), `write-tree` = `c7fb3c0f1a588b268115bf0dceb24f0c26eb1ae0` (verified to match). Submodules were being fetched (`git submodule update --init --recursive --depth 1`); confirm they're complete.
   - `third_party/fear-nx/src-repo/`: their repo at `acad80d`. The renderer is in `src/native/`; `#if defined(__SWITCH__)` guards show it was meant to build elsewhere too, and on non-Switch `nx_renderer` defaults to `"xenos"` (`--nx_renderer native` selects theirs).
   - **Blocker:** on Windows the SDK builds `rexgpu-xenos` as a **SHARED plugin with everything PRIVATE** (`src/graphics/CMakeLists.txt` ~l.102), but their renderer needs the graphics internals linked in (on Switch it's STATIC and `gpu_plugin_loader.cpp` calls `rex_gpu_create` directly). Make it STATIC on Windows too, enable `REXGLUE_USE_VULKAN`, build the port with `-DREXSDK_DIR=third_party/rexglue-sdk-nx` in its own binary dir, add `src/native/*.cpp` + include dirs (`${REXSDK_DIR}/thirdparty/{imgui,glslang,xxHash}`, `${REXSDK_DIR}/src/graphics`), and in `FearApp::OnPreSetup` set `config.graphics = fear_nx::native::CreateGraphicsSystem()` when `fear_nx::native::Enabled()`. Their CMake (`third_party/fear-nx/src-repo/CMakeLists.txt`) is the Switch reference. Codegen will rerun with that SDK's own `rexglue` (0.10.0).
   - Licensing: using their renderer makes those parts GPL-3.0; note it in NOTICE (as the Quake 4 repo does for its patches).

## Running it
- Build: `pwsh -NoProfile -File tools/build.ps1` (DevShell + `.tools/` junction to the N3 toolchain, ReXGlue nightly `bd833a2` prebuilt).
- Play: `pwsh -NoProfile -File tools/launch.ps1 [-Scale 1..3] [-Windowed]` (fullscreen, `--resolution 720p` for the high-DPI 4:3 fix, 16x AF).
- Tests: `pwsh -NoProfile -File tools/smoke.ps1 -Seconds 50 -Captures 4 -Extra '--flag ...'` runs its own windowed copy (own profile and cache), screenshots it, then stops it. Warn the user first: the window appears on their screen, and they may otherwise play in it, thinking it's theirs.
- **Controller:** a Logitech virtual XInput pad (`046D:0000`, "HID-compliant game controller") holds slot 0, so the Xbox pad is player 2 and the game says "No profile selected". The user is disabling it.

## Goal reminder (from the user)
A full Windows (then Linux) PC version in the spirit of OpenCE: point it at your own files, launch, and get real options (4K, widescreen, anisotropic filtering, frame rate, controls). Tune for the user's machine first (Ryzen 7 9800X3D, RTX 4090, 4K 240 Hz at 200% scaling), then scale down. fear-nx's Switch-specific performance cuts are not wanted.
