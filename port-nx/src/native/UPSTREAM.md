Copied from https://github.com/infriker/fear-nx (GPL-3.0-or-later), commit acad80d
(2026-10-21), `src/native/`. Their renderer is adapted from Project8Recomp's generic
Xenos Vulkan renderer, itself based on nfsmw-nx.

Local changes (see `git log -p -- port-nx/src/native`):
- Integer draw-resolution scaling (`nx_native_resolution_scale`, 1-4): host render
  targets, resolve images and the swap are scaled; guest-space math is untouched.
- Texture pack hooks in `native_textures.cpp` (marked `F.E.A.R.`): a replacement key per new
  texture content, a streamed upload from the pack's DDS instead of untiling guest memory,
  no in-place refill of replaced images, and a per-entry host swizzle for replacements whose
  channel layout differs from the 360 host format's. The pack itself is
  `port-nx/src/texture_pack.*`.
- `MirrorAliasClear` in `native_renderer.cpp`: colour/depth/stencil clears drawn through an
  aliased layout of the same EDRAM (Direct3D's 4x-MSAA fast clear of a 2x surface) are
  applied to every host image of that EDRAM. Fixes doubled text/cards and half frames.
  `GetFastClearRectangle` also returns the rectangle's depth.
- `nx_native_trace_frame_every`: a full log (draws with their textures, clears, resolves,
  swaps) of one frame in N.
- Texture streaming on PC (`native_textures.cpp`, marked `F.E.A.R.`): a draw waits for a
  texture still streaming in (`fear_texture_stream_wait_ms` per frame) instead of using the
  empty texture, which flashed; 6 stream workers instead of 2 off Switch.
- Render-thread timing in the 10-second report (swap, present, queue lock, streaming waits).
- `fear_shader_wait_ms` (100): draws wait for their shader translation/pipeline within a
  per-frame budget instead of being skipped. `fear_alias_clears`/`_depth`: kill switches for
  MirrorAliasClear. Submit failures log their VkResult.
- Direct texture uploads (`fear_texture_upload_direct`, PC): every new texture, and every
  texture-pack replacement, is created on the spot and filled by 6 upload workers before the
  submission (fear-nx's path for two small textures a frame); stream-job step timing in the
  10-second report.
- Fixes found with the validation layer: the texture cache's frame number is synced to the
  renderer's at startup (it was 0 vs 1, so first-frame upload buffers were freed while the
  GPU read them: device lost), and the constant-buffer descriptors are written one binding
  per write (one write across bindings with different stage flags is invalid).
- Display-locked pacing in `native_system.cpp` (marked `F.E.A.R.`): with
  `fear_vblank_source=display` the vblank thread waits on the display's vblank
  (`DisplayVblankLoop`, `port-nx/src/display_vblank.*`) instead of a timer, `PaceToDisplay`
  holds each frame to whole refreshes before presenting, and the 10-second report gives a
  `pacing` line (refreshes between presents, counted from the display in both modes).

- Texture eviction by time (`native_textures.cpp`, `BeginFrame`, marked `F.E.A.R.`): textures
  unused for 20 seconds are evicted, measured by the clock rather than as 600 frames (20 s at
  30 fps, but 7.5 s at 80, which re-created a room's textures on the way back). See
  `docs/REVIEW.md`.

- Shader pre-warm (`fear_shader_prewarm`, `native_renderer.cpp`, marked `F.E.A.R.`): every
  translation a run asks for is recorded with its shader's microcode and saved per title and
  device next to the driver cache; at the next start they are queued for the translation worker,
  behind any draw is waiting for. See `docs/REVIEW.md`.

The `*_test.cpp` files are kept for reference but are not built here.
