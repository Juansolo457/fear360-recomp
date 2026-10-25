# d3d12/: F.E.A.R.'s D3D12 renderer

See [docs/NATIVE_RENDERER.md](../../../docs/NATIVE_RENDERER.md) for the design and progress.

## Provenance and licenses

These files are ported from **GRAW 360 Recompiled**'s `native/`
(https://github.com/Juansolo457/graw360-recomp, commit `1b42337`, 2026-10-25, MIT), which ports
them from **pgr4-native** (https://github.com/shaunsingh/pgr4-native), commit `8ea20d5`
(2026-10-25), `src/native/`. pgr4-native's license (MIT) is in
[LICENSE.pgr4-native](LICENSE.pgr4-native).

GRAW's changes to pgr4-native: its own D3D front end (a push-buffer stream tracker,
`d3d_state.*`, in place of pgr4-native's device-shadow tracker and PGR4 hook table), and the
fixes in its docs/NATIVE_RENDERER.md (`k_10_11_11` textures, SDR output, the push-buffer wrap,
`BeginIndexedVertices`' HRESULT, the presenter hand-back). The changes for F.E.A.R.:
- renamed: namespace `graw::native` to `fear::d3d12`, options `graw_native*` to `fear_d3d12*`,
  log prefix `GRAW native` to `D3D12`, cache folders `graw-native` to `fear-d3d12`;
- F.E.A.R.'s Direct3D: `fear_d3d.h` (addresses, device layout, dirty bits, object layout) and
  `fear_d3d_hooks.cpp` (the hook table, with F.E.A.R.'s second EndVertices);
  `render_targets.h` takes its device fields from `fear_d3d.h` (tile origins are 8 bytes here);
- fear-nx is the other renderer (GRAW's is ReXGlue's Xenos plugin): `gpu_skip_draws` is
  fear-nx's ring switch (`../native/native_system.cpp`), the gamma ramp comes from fear-nx
  (`present.cpp`), and F.E.A.R.'s own panels hand the game to fear-nx while they are open
  (`renderer.cpp`, `HandOverDue`);
- GRAW's separate build of the SDK's shader translator (`shader_xlate.cmake`, `sdk_cvar_shim.h`)
  is not needed: F.E.A.R. links the Xenos plugin, translator included, into the executable.

pgr4-native ports code from the ReXGlue SDK and, through it, from Xenia (BSD-3-Clause; the
ported functions are named in each file's header): among others `draw`, `render_targets`,
`resolve`, `geometry_shader` and the texture code. The ReXGlue SDK's license, with Xenia's
notice, applies to those parts:

```
Copyright (c) 2026, Tom Clay <tomc@tctechstuff.com>

Portions of this software are derived from the Xenia project:
Copyright (c) 2022, Ben Vanik and Xenia project contributors
https://xenia.jp

All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:

1. Redistributions of source code must retain the above copyright notice, this
   list of conditions and the following disclaimer.

2. Redistributions in binary form must reproduce the above copyright notice,
   this list of conditions and the following disclaimer in the documentation
   and/or other materials provided with the distribution.

3. Neither the name of the copyright holder nor the names of its
   contributors may be used to endorse or promote products derived from
   this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE
FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
```
