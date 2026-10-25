# D3D12 renderer (branch `native-renderer`)

Started 2026-10-25. F.E.A.R. gets the renderer GRAW 360 got in
[graw360-recomp#26](https://github.com/Juansolo457/graw360-recomp/pull/26): a Direct3D 12 renderer
that draws from the game's own Xbox 360 Direct3D calls, ported from
[shaunsingh/pgr4-native](https://github.com/shaunsingh/pgr4-native). **1.0 waits on it**: the
renderer, then a performance test against today's, then the full smoke test (#55-#57).

## Where F.E.A.R. starts from

GRAW replaced ReXGlue's Xenos plugin (Xenia's emulated GPU, its command processor and register
file). F.E.A.R. left that path early: it draws with fear-nx's renderer (`port-nx/src/native/`),
which reads the PM4 ring in-process and records Vulkan. So the gain to look for here is not GRAW's
whole gain, but its part that fear-nx still pays:

- **Tile replays.** F.E.A.R.'s Direct3D uses predicated tiling like GRAW's (below): it records a
  frame once and replays it for each EDRAM tile, and fear-nx runs a predicated packet in every tile
  whose bin select matches (`native_system.cpp`, `Type3Packet`), as Xenia does. GRAW measured
  draws x2 and register writes x2.2 from this. An API-level renderer takes each draw once.
- **PM4 parsing and the register file** on the ring thread (4-8 ms a frame in the Deck's heavy
  fights, `ARCHITECTURE.md`).
- **Pipelines** built from the game's own state, compiled on a worker and pre-warmed.

What it doesn't change by itself: the game thread. The game's Direct3D still builds its packets
(GRAW's front end decodes them at each hooked call). On the Deck the game thread is the first
limit in the big fights (about 19 ms a frame), so a game-thread gain needs pgr4-native's draw
replacement (`guest_d3d`, which skips the packet building) later. And on the Deck, Direct3D 12
runs through vkd3d-proton where fear-nx's Vulkan is native: measure there before it becomes the
Deck's default.

## F.E.A.R.'s Direct3D (August 2006 XDK, `0x82960000`-`0x829BFFFF`)

All in [port-nx/src/d3d12/fear_d3d.h](../port-nx/src/d3d12/fear_d3d.h). Found by pairing each of
GRAW's D3D functions with its twin here by instruction shape, then aligning each pair instruction
by instruction and reading the device offsets off the matching loads and stores (MakeSpace and the
segment restore line up exactly). Functions (r3 = device):

| Function | Role | GRAW's |
|---|---|---|
| `sub_829873D8` | BeginVertices (prim, count, stride) | `sub_824C4578` |
| `sub_82987890`, `sub_82987E60` | EndVertices, after each of the two Begins (GRAW has one) | `sub_824C4818` |
| `sub_829878B0` | DrawVerticesUP (BeginVertices + copy + commit) | `sub_824C4838` |
| `sub_82987910` | BeginIndexedVertices, returns an HRESULT | `sub_824C48C0` |
| `sub_82987F38` | DrawVertices (prim, start, count) | `sub_824C4BF8` |
| `sub_82988340` | DrawIndexedVertices (prim, base vertex, start index, count) | `sub_824C4E38` |
| `sub_829872B0` / `sub_82986F00` | Clear / ClearF | `sub_824CDCC8` / `sub_824CD598` |
| `sub_82984718` | Resolve | `sub_824D1960` |
| `sub_82974598` | Swap (`VdSwap`) | `sub_824CC9C8` |
| `sub_82979BD0` / `sub_8297A0C8` | BeginTiling / EndTiling | `sub_824D2868` / `sub_824D2CA0` |
| `sub_8298A988` | MakeSpace (submits through `sub_8298A7E8`) | `sub_824D7E80` |
| `sub_8298A620` / `sub_82989998` | wrap to the buffer start / restore a saved segment | `sub_824D7C08` / `sub_824D6F88` |

Not hooked: `sub_82987E80` (BeginIndexedVertices + copy + EndIndexedVertices, which the hooks
see), and `sub_829887B8`, `sub_82988C50` (two more draw builders nothing calls directly; the
tracker logs any draw packet no hooked call wrote).

Device, against GRAW's: the push cursor at `+40` (GRAW `+0`, PGR4 `+48`), its segment end and
MakeSpace limit at `+44`/`+48`; five dirty masks at `+0..+32` (GRAW `+16..+48`, with other bits:
`kDirtyBits`, read from the draws' inline flush; GRAW's draws call a flush function); the
constant shadows 128 bytes lower than GRAW's (fetch `+1024`, VS `+1792`, PS `+5888`, bool/loop
`+9984`), the register shadow blocks 1,216 lower (0x2000 at `+10240`); flags at `+10808`; index
buffer, colour and depth surfaces at `+12164`..`+12184`; the tile list at `+12472` with 8-byte
tile origins as in PGR4's D3D (GRAW keeps 16; **to verify** at the first run, with the resolve
oracle); BeginVertices' pending cursor at `+13076`. The resource header is 24 bytes, as in PGR4's
XDK (GRAW's 12): textures' fetch constants at `+28`, an index buffer's address at `+24`.

F.E.A.R.'s segment submit has a branch GRAW's lacks (flags `+10809` bit `0x40`: segments recorded
into a list at `+13160` instead of submitted), probably the D3D's command buffer recording; to
watch in the tracker's counters.

About 17 functions outside the Direct3D appear to write packets themselves (`ARCHITECTURE.md`;
engine `82174980-821798B0`, ClientFX's particle batcher `sub_82BB65E0`; not checked one by one
yet). If they write through the same push cursor, their packets are in the stream at the next
hooked call and the decoder sees them.

## What fear-nx does that the new renderer must do too

Before it can be the default, in both games:

- Resolution scale (`nx_native_resolution_scale`), mirrors and screens with it (#25); MSAA on or off.
- PC textures (the texture pack), texture streaming without empty-texture flashes, mipmap bias (#50).
- Shader and pipeline warm-up (#53) and pre-warm: pgr4-native's pipeline store does this its way.
- Display-locked pacing and the frame rate settings (`fear_vblank_source`, #71).
- `MirrorAliasClear`: the 4x-MSAA fast clear through an aliased surface (doubled text, half frames).
- Fresh vertex data (#27, #28): at the API the draw's data is read at the call, so likely moot.
- The video planes (#66), the bloom setting (#38, a game hook, renderer-independent), fear-nx's
  frame trace and the 10-second report's counters.

## Steps (each ends in a measurement)

1. **Measure the tile replays and today's cost.** Counters built (2026-10-25), not run yet.
   `--fear_tile_stats=true` adds a line to the 10-second report: draws and register writes run per
   frame against the unique ones, draws skipped outside their tile, bin selects and tiles. Run it
   with the existing renderer counters in the same scenes on PC (4K) and the Deck: the baseline for
   step 5.
2. **Map the Direct3D.** F.E.A.R. done (above). F.E.A.R. Files' addresses differ: to do, the same
   way.
3. **Vendor and build.** Done for F.E.A.R. (2026-10-25), not run yet. GRAW's `native/`
   (pgr4-native `8ea20d5` plus GRAW's fixes: `k_10_11_11` textures, SDR output, the push-buffer
   wrap, `BeginIndexedVertices`' HRESULT, the presenter hand-back) as `port-nx/src/d3d12/`; the
   two shader translator patches as `patches/rexglue-sdk-nx/0016`, `0017` (GRAW's other two are
   for the Xenos plugin: fear-nx has its own `gpu_skip_draws` and gives the presenter its gamma
   table); hooks that replace the Direct3D functions (`fear_d3d_hooks.cpp`, no manifest change).
   - `--fear_d3d12`: a debug window beside fear-nx, which stays the game's output
     (`tools/launch.ps1 -Nx -D3D12Side`).
   - `--fear_d3d12_primary`: the D3D12 renderer draws into the game window, F9 back to fear-nx and
     again (`-Nx -D3D12`). fear-nx's ring keeps the fences, interrupts, swaps and pacing and skips
     its own draws (`gpu_skip_draws`), and draws again while one of F.E.A.R.'s panels (PC
     Settings, the collection menu) is open, since those are ImGui in ReXGlue's presenter.
4. **First runs**: side by side with fear-nx (capture pairs, PSNR), then primary with F9 back to
   fear-nx. Autopilot through the opening and Interval 2's heavy fights.
5. **Parity and performance**: the list above, then PC and Deck against step 1.
6. **Decide the default** per platform, then the smoke test and 1.0.

Licensing: GRAW 360 Recompiled and pgr4-native are MIT, the Xenia- and ReXGlue-derived parts
BSD-3-Clause: `port-nx/src/d3d12/README.md`, `NOTICE` (4) and `LICENSES/PROVENANCE.md`.
