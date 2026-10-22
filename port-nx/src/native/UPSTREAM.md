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

The `*_test.cpp` files are kept for reference but are not built here.
