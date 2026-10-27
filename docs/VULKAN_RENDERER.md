# The renderer on Vulkan, and the native Linux build

Started 2026-10-26, on branch `vulkan-linux`. The renderer (`port-nx/src/d3d12/`,
[NATIVE_RENDERER.md](NATIVE_RENDERER.md)) gets GRAW 360's Vulkan backend, so Linux and the Steam
Deck run the game natively instead of through Proton. GRAW did the work first
([graw360-recomp#44](https://github.com/Juansolo457/graw360-recomp/pull/44), its
`docs/VULKAN_RENDERER.md` has the plan, the milestones and what each one found); this is the port
of it to F.E.A.R. **Status: written, not built or run yet.**

## Decisions (2026-10-26)
- **D3D12 stays the Windows default.** Vulkan on Windows is a comparison flag only,
  `--fear_d3d12_api=vulkan`, kept out of the PC Settings panel.
- **Linux and the Steam Deck are native Vulkan only:** no D3D12, no Proton or Wine path for this
  build, no switch.
- As in GRAW: the Deck runs the 360 game on the native renderer, set up to hold its frame rate,
  without PC extras (the Deck baseline stays: no MSAA, the 360's textures).

This amends [NATIVE_RENDERER.md](NATIVE_RENDERER.md)'s "D3D12 only" (step 6): one renderer
still, now with two GPU APIs behind it.

## What came from GRAW
GRAW's renderer at `0377b6c`, merged into F.E.A.R.'s copy of it (three-way, from the `1b42337`
F.E.A.R. was ported from):
- **The backend split** (GRAW's M1): `backend.h`, the interface; `renderer.cpp` is the API-free
  host (hooks, render thread, statistics and CSV); `d3d12_backend.*` holds the D3D12 device and
  modules, `vulkan_backend.*` the Vulkan ones.
- **API-free code out of the D3D12 files** (M3b, M7a): `surfaces.*` (surface registers, decoded
  clears, EDRAM fills), `resolve_params.*` (`DecodeResolve`), `present_swap.cpp` (`ReadSwap`),
  `guest_draw_state.*` (viewport, scissor, depth, blend, sampler keys), `guest_memory_mirror.*`
  (the page watches and range copies), `gamma_ramp.*`, `pipeline_library.*`, `native_cvars.cpp`,
  `host_types.h`, `native_thread.*`.
- **The Vulkan backend** (M2-M5): device and frames (`vulkan_device.*`), VMA memory
  (`vulkan_memory.*`), the SPIR-V translator's binding model (`vulkan_bindings.*`), shader
  modules and variants (`vulkan_shaders.*`, `spirv_translate.*`, `shader_modifications.*`),
  pipelines with background compiles and a `VkPipelineCache` (`vulkan_pipelines.*`), render
  targets on dynamic rendering (`vulkan_render_targets.*`), textures (`vulkan_textures.*`),
  geometry shaders (`vulkan_geometry_shader.*`), resolves (`vulkan_resolve.*`) and the present
  pass (`vulkan_present.*`), with their GLSL shaders in `shaders/` (headers built by
  `tools/vulkan_shaders.py`, committed).
- **SDK patches** (`patches/rexglue-sdk-nx/`): 0019, the DXBC translator's implicit-gradients
  switch (`--fear_d3d12_implicit_gradients`, a Deck shimmer experiment); 0020, the SPIR-V
  translator's exponent adjustment from the right fetch constant word; 0021, the title launch
  path kept in a string (it pointed into freed memory: on GRAW's Deck run the requested path held
  glibc's free-list pointers).
- **Linux build scripts:** `tools/build-deck.sh` and `tools/linux/deck.Containerfile` (an Ubuntu
  22.04 container, so the game needs glibc 2.35 at most), the process restart on Linux
  (`posix_spawn`, `fear_process.cpp`), `tools/vulkan_shaders.py`.

## What is F.E.A.R.'s own
- **Both backends record on ReXGlue's presenter's device and hand it the frames.** F.E.A.R. had
  already done this on D3D12 (NATIVE_RENDERER.md step 6), so its panels (the collection menu,
  PC Settings, the pipeline warm-up, the version badge) and the system dialogs draw over the game
  in the presenter. The Vulkan backend does the same with ReXGlue's Vulkan presenter: fear-nx's
  graphics system (`port-nx/src/native/native_system.cpp`) creates the provider for the
  renderer's API (`fear::d3d12::RendererApi()`), `VulkanDevice` takes that provider's instance,
  device and graphics queue (under the SDK's queue lock), and `EndFrame` draws the present pass
  into the presenter's guest output inside `RefreshGuestOutput`, at the front buffer's size; the
  presenter scales it to the window. So GRAW's own instance, swapchain, Wayland/X11 surface code,
  primary mode, F9 switch and game window hand-over are not ported (`game_window.*`), and neither
  is the frame pacer (F.E.A.R. creates none: the presenter paces).
- **SDK built from source with Vulkan:** F.E.A.R. already builds the SDK with `REXGLUE_USE_VULKAN`
  and links the Xenos plugin statically (fear-nx's origin), so the SPIR-V translator, glslang and
  VMA come from it; GRAW's in-app translator build and its Linux shim (`sdk_linux_shim.cpp`) are
  not needed. Patch 0018 makes the SDK's Vulkan device enable `textureCompressionBC` and
  `shaderStorageImageWriteWithoutFormat`, which GRAW's own device enabled.
- **Implicit gradients on SPIR-V:** fear-nx's SDK patch already had them, behind the translator's
  `gpu_implicit_lod_sampling` (2D and cube textures; GRAW's patch also covers 1D). The Vulkan
  backend sets it from `--fear_d3d12_implicit_gradients`.
- **The gamma ramp** comes from fear-nx's ring (the 256-entry table only), as on D3D12.
- **F.E.A.R.'s renderer fixes, carried into the shared code or the Vulkan backend:**
  - side-by-side predicated tiles: render targets as wide as their draws reach, the tiled scissor
    unclamped, a resolve's clear clipped at the next tile (`surfaces.*`, `guest_draw_state.cpp`,
    `vulkan_render_targets.cpp`, `vulkan_backend.cpp`, `resolve_params.*`);
  - `--no-fear_d3d12_msaa` (the Deck baseline): host targets at one sample, the EDRAM layout at
    the guest's (`HostSamples`, also in the SPIR-V system constants);
  - `fear_mip_bias` (texture sharpness) and `fear_d3d12_anisotropy` on the Vulkan samplers;
  - `k_4_4_4_4` textures (fonts, HUD) as `VK_FORMAT_A4R4G4B4_UNORM_PACK16` (core in Vulkan 1.3,
    optional: a GPU without it fails those textures, logged);
  - the shader load bound (a ucode address past physical memory isn't read);
  - the pipeline warm-up before the first draw, with its panel (`fear_d3d12_warmup`).
- **One build file for both games:** `port-nx/cmake/fear_common.cmake` (the renderer's object
  library, Windows: every file, Linux: without the D3D12 backend's; the static plugin linked whole;
  the runtime libraries staged beside the program on Linux).

## Building
- **Windows:** unchanged, `tools/build-nx.ps1` (D3D12 by default; `--fear_d3d12_api=vulkan` to
  compare). Patches 0018-0021 need a fresh `tools/setup-sdk.ps1 -Force`, or `git am` of the four
  onto an existing `third_party/rexglue-sdk-nx`.
- **Linux x86-64:** `tools/setup-sdk.sh`, then `tools/build-linux.sh` and
  `tools/build-linux.sh --project port-files` ([SETUP.md](SETUP.md), "Linux").
- **Steam Deck:** `tools/build-deck.sh` (podman), with its own SDK checkout and build folders.

## Not done yet
- **Building and running any of it.** The order: Windows D3D12 first (must be unchanged), then
  Windows Vulkan against D3D12 (captures, frame times), then Linux (WSL), then the Deck.
- On Vulkan, as in GRAW: GPU timing (`--fear_d3d12_gpu_timing`), the capture tools and the luma
  probe, HDR, texture eviction, untiling on worker threads.
- The Deck package (`tools/steamdeck/make_package.py`) still packs the Windows builds for Proton;
  a native package (the Linux program, its two shared libraries, a launcher) comes after the first
  Deck run.
- On Linux: the sampling profiler (`fear_profile`, Windows only), the start-up CPU check
  (`cpu_check.cpp`), the display's vblank (fear-nx's timer pacing only).
- Renaming `port-nx/src/d3d12/` and `fear::d3d12` now that it is not D3D12 only.
