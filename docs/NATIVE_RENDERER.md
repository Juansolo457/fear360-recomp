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

From GRAW's `tools/scan_d3d.py` over the generated code (71 functions build type-3 packets):

| Function | What it is | Evidence |
|---|---|---|
| `sub_829873D8`, `sub_82987910`, `sub_82987F38`, `sub_82988340`, `sub_829887B8`, `sub_82988C50` | the draw entry points (GRAW has four) | predicated `DRAW_INDX` with `SET_BIN_MASK_LO` and predicated `EVENT_WRITE(_EXT)`; callers in the engine renderer (`8229A6F0`, `8229A7A0`, `822C0668`, ...) |
| `sub_82984718` | Resolve or Clear | `IM_LOAD_IMMEDIATE`, predicated `DRAW_INDX_2`, `WAIT_REG_MEM`, `EVENT_WRITE` |
| `sub_82985F80`, `sub_8298F8C0`, `sub_82993020`, `sub_829859E0` | internal rectangle draws | `DRAW_INDX_2` with immediate shaders |
| `sub_8296D830`, `sub_82996910` | tiling: bin mask and select | `SET_BIN_MASK_*`, `SET_BIN_SELECT_*` |
| `sub_82977798` | the tile replays | `INDIRECT_BUFFER(pred)`, `SET_BIN_SELECT_HI` |
| `sub_82989CE8` | indirect buffer | `INDIRECT_BUFFER` |
| `sub_829701E8` | occlusion query | `EVENT_WRITE_ZPD` |
| `sub_82974598` | Swap | calls `VdGetSystemCommandBuffer` and `VdSwap` |
| `sub_8298AF08` | ring buffer set-up | `VdInitializeRingBuffer`, `VdEnableRingBufferRPtrWriteBack`, `ME_INIT` |
| `sub_8298D1E8` | device start-up | `VdSetGraphicsInterruptCallback` |

Device, first look (`sub_829873D8`): five 64-bit masks cleared at `+0..+32` (PGR4 keeps its dirty
masks at `+0..+40`; GRAW's start at `+16`), the flags at `+10432` (GRAW's too), the index buffer at
`+12420` (GRAW `+12532`, PGR4 `+12428`). So the layout is its own, between the two. GRAW's front
end needs only a few offsets (the push cursor, the segment functions, the surfaces), since it
decodes the packets the Direct3D writes rather than its device. About 17 functions outside the
Direct3D appear to write packets themselves (`ARCHITECTURE.md`; engine `82174980-821798B0`,
ClientFX's particle batcher `sub_82BB65E0`; not checked one by one yet). If they write through the
same push cursor, their packets are in the stream at the next hooked call and the decoder sees them.

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

1. **Measure the tile replays and today's cost.** `--fear_tile_stats=true` adds a line to the
   10-second report: draws and register writes run per frame against the unique ones, draws
   skipped outside their tile, bin selects and tiles. Run it with the existing renderer counters
   in the same scenes on PC (4K) and the Deck: the baseline for step 5.
2. **Map the Direct3D**: the draw entry points' arguments, Clear, Resolve, BeginTiling/EndTiling,
   MakeSpace and the buffer wrap, the device's push cursor and surfaces (GRAW's `graw_d3d.h` table
   is the model). Both games: F.E.A.R. Files' addresses differ.
3. **Vendor and build.** GRAW's `native/` (pgr4-native `8ea20d5` plus GRAW's fixes: `k_10_11_11`
   textures, SDR output, the push-buffer wrap, `BeginIndexedVertices`' HRESULT, the presenter
   hand-back) as `port-nx/src/d3d12/`, its four SDK patches adapted to `rexglue-sdk-nx`, the
   hooks in both manifests. `nx_renderer` gets a third value, `d3d12`. fear-nx's ring thread keeps
   the fences, interrupts and swaps (as GRAW's Xenos plugin does with `gpu_skip_draws`) and skips
   its own draws.
4. **First runs**: side by side with fear-nx (capture pairs, PSNR), then primary with F9 back to
   fear-nx. Autopilot through the opening and Interval 2's heavy fights.
5. **Parity and performance**: the list above, then PC and Deck against step 1.
6. **Decide the default** per platform, then the smoke test and 1.0.

Licensing: pgr4-native is MIT, its Xenia- and ReXGlue-derived parts BSD-3-Clause; the licence
files and `LICENSES/PROVENANCE.md` come with the vendoring.
