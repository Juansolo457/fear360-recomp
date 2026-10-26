# D3D12 renderer (branch `native-renderer`)

Started 2026-10-25. F.E.A.R. gets the renderer GRAW 360 got in
[graw360-recomp#26](https://github.com/Juansolo457/graw360-recomp/pull/26): a Direct3D 12 renderer
that draws from the game's own Xbox 360 Direct3D calls, ported from
[shaunsingh/pgr4-native](https://github.com/shaunsingh/pgr4-native). **1.0 waits on it**: the
renderer, then a performance test against today's, then the full smoke test (#55-#57).

## Status (2026-10-25)

First runs on PC (RTX 4090, autopilot through a new game's opening to the hangar, 120 fps, 720p
window), side by side with fear-nx (`--fear_d3d12`):
- **The front end works on F.E.A.R.'s Direct3D as mapped**: 760 million push-buffer dwords decoded
  per run with no resyncs, torn packets, unknown opcodes or draws outside the hooks.
- **The picture matches fear-nx's** (play-tested: "basically 1:1 the 360 version") after three
  fixes for F.E.A.R. tiling side by side where GRAW tiles in full-width bands: host targets as
  wide as the untiled draws, no scissor clamp to the surface pitch (one tile's width while
  tiling), and EndTiling's clears kept off the next tile's overlap (the first tile is 768 wide,
  the next starts at 640). `k_4_4_4_4` textures added: F.E.A.R.'s fonts (the intro's subtitles).
- **The briefing screen's flicker (#66) is gone**: in a recording of both windows, the screen in
  fear-nx's alternates between two brightnesses every other frame (105 changes in 211 frames);
  the D3D12 renderer's holds steady, and shows the video's camera feeds where fear-nx's shows the
  logo.
- **Cost, same scenes**: fear-nx's ring runs each draw 1.4-2.0 times and each register write
  1.5-2.0 times (2-3 tiles a frame; `--fear_tile_stats`), and its renderer thread uses about
  1.1 ms of CPU a frame; the D3D12 renderer draws each once, about 0.26 ms of recording and 0.2 ms
  of present a frame, adding about 70 us to the game thread for the hooks.
- Open: `k_5_6_5` resolves (no typed UAV store for B5G6R5), the primary mode (`-D3D12`) not run
  yet, the parity list below, F.E.A.R. Files, the Steam Deck, and what shipping D3D12 alone takes
  (step 6).

## Steam Deck run, D3D12 only (2026-10-25, the first four levels)

What the user saw, and what the log (`fear_031.log`) adds:
- **Good:** the black bars are gone, the subtitles show, the briefing screen is steady (#66),
  the bullet impact sparks and dust show (missing in every fear-nx build), animation looks like the
  360's, the Sharp AI fights well. The renderer came up on vkd3d-proton (RADV) with no warnings or
  errors in the whole session.
- **The run was the 360 baseline already:** the Deck's `user/settings.toml` (the Deck package's)
  sets none of the PC Settings options, so field of view, model detail, bodies, glass, weapon
  slots and the rest were as shipped. They are game hooks and work with any renderer; only blood
  marks and the Sharp AI default on.
- **Frame time:** median 8.4 ms, p90 13.2 ms; windows over 500 draws median 15.4 ms. The game
  thread is the limit in the heavy scenes (84-88% of a core), and part of it is the D3D12
  renderer's guest memory watch under Proton: page protection calls (6-10% of the game thread),
  the exception dispatch of the writes it catches (2-3%) and its "D3D12 watches" thread (12% of a
  core). fear-nx hashed instead of watching pages.
- **The spikes when shooting enemies:** first-run pipeline builds, 40 ms median and up to 175 ms
  each on the Deck, which a draw waits for (`fear_d3d12_pipeline_wait`): the blood, sparks and
  decals appear the first time something is shot. 622 pipelines were stored in
  `cache/fear-d3d12/pipelines.d3d12lib`, so the next run should hitch far less.

## Native punch list

The features that were fear-nx's, and what the Deck run turned up. The game-side PC Settings
options (FOV and gun position, model detail, bodies and glass, weapon slots, flashlight, intro
videos, cutscene bars, Sharp AI, bloom) need nothing: they are hooks on the game's code.

| # | Item | Kind | Notes |
|---|---|---|---|
| 1 | Blood and armour marks on enemies (#47) | bug | Deck only: on the PC (2026-10-25, 17:28) the marks show, with 0 draws dropped. On the Deck `ModelDecalPerformanceLevel = 3` is applied (log) but no marks showed. Ruled out by reading: the depth bias (same sign and size as fear-nx's), float24 depth (fear-nx didn't convert either: its flag only feeds the interlock path), draws through the two unhooked draw builders (nothing builds their addresses). Suspect: how AMD's driver under Proton applies the depth bias. Next: a Deck F12 draw log of a frame with a fresh mark, against a PC one |
| 2 | Subtitles disappear when the opening scene is paused | bug | check fear-nx's and the 360's behaviour first; likely the pause screen's copy of the frame |
| 3 | MSAA off (the Deck's baseline before) | parity | done, not run: `fear_d3d12_msaa` (PC Settings > Anti-aliasing), host targets with one sample, the EDRAM layout still the guest's; the Deck package's settings turn it off |
| 4 | Pipeline hitches on first sight | parity | done, not run: `fear_d3d12_warmup` builds every stored pipeline before the first draw on all the pipeline threads, with the "PREPARING SHADERS" panel (`src/pipeline_warmup.*`) |
| 5 | Guest memory watch cost under Proton | performance | the page-protection watch: batch it, or hash like fear-nx on Proton |
| 6 | Resolution scale (PC: 4K) | parity | PC Settings row removed until then |
| 7 | Texture sharpness (#50, mip bias) | parity | done, not run: `fear_mip_bias` again (fear-nx's name, so a settings file keeps its choice; -0.5 by default), each sampler's `MipLODBias` (`draw.cpp`), on top of the bias the translated shaders apply; the PC Settings row is back |
| 8 | PC textures (texture pack) | parity | the pack's replacement in the D3D12 texture cache; row removed until then |
| 9 | `k_5_6_5` resolves | gap | no typed UAV store for B5G6R5 (logged once) |
| 10 | Bodies flung when bullet time ends (#62, #4) | game | not the renderer: high frame rates (seen toward 100 fps) |
| 11 | Load in slow-mo firefights | performance | PC run 2026-10-25 18:36 (RTX 4090, 120 fps, MSAA on, 16x anisotropic, "Bodies and debris: Stay" on): the user saw GPU use go from under 10% to over 20% in slow-mo fights with explosions. The log: draws per frame went from 150-700 to 1,100-2,100 (18:39:25-18:39:48, 18:40:51-54, 18:41:02), frames from 8.33 to 9.3-10.9 ms, the game's GPU waits from 8-10 s to 2.9 s per 10 s, and the game thread's time in the guest D3D from 0.3 to 1.7 ms a frame. So those frames were held up on the CPU side; the GPU's share isn't measured yet. Next: the same fight with `--fear_d3d12_gpu_timing` and `--fear_d3d12_csv`, once with "Bodies and debris" on Stay and once on Fade (lasting debris adds draws), then the laptop test with everything turned up |

Then, as planned: a Deck run with every option on (a Deck `settings.toml` that turns them all on),
a performance check against the first runs, the smoke test (#55-#57) and 1.0.

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
   frame against the unique ones, draws skipped outside their tile and tiles. Run it
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
6. **D3D12 only** (decided 2026-10-25, done in the code the same day, not built yet): the game
   ships with the D3D12 renderer alone, no fear-nx or ReXGlue renderer and no switch.
   - fear-nx's graphics system (`port-nx/src/native/native_system.*`) keeps the GPU ring and owns
     ReXGlue's **D3D12 presenter** instead of its Vulkan one. The D3D12 renderer records on the
     presenter's device and queue and hands it each frame (`Presenter::HandToPresenter`: the
     front buffer blitted with the gamma table into a 10:10:10:2 image, copied into the
     presenter's guest output in its refresh callback). The presenter keeps the window and draws
     F.E.A.R.'s ImGui panels and the system dialogs over the game, so nothing hands the window
     back and forth.
   - Removed: fear-nx's Vulkan renderer, its shader warm-up (`shader_warmup.*`,
     `tools/make_warmup.py`), the texture pack's runtime hooks (the offline indexer stays), the
     side-by-side window, primary mode, F9, `nx_renderer`, `gpu_skip_draws`. PC Settings loses
     Resolution, PC textures and Texture sharpness until the D3D12 renderer has them; Texture
     filtering now sets `fear_d3d12_anisotropy`.
   - **F.E.A.R. Files** mapped the same way (F.E.A.R.'s functions paired with Files', `fear_d3d.h`
     under `FEAR_TITLE_FILES`): push cursor at +48, constant shadows where GRAW has them, register
     shadows 128 bytes above F.E.A.R.'s, surfaces 264 above, its own dirty bits.
   What it still takes:
   - The parity list above done in the D3D12 renderer, since nothing falls back to fear-nx.
   - The Steam Deck: Direct3D 12 through vkd3d-proton is then the Deck's only path, so it is
     tested and measured there next.
   - `tools/steamdeck/make_package.py` still writes fear-nx's options (harmless: unknown options
     are ignored) and the Deck's MSAA-off choice has no D3D12 equivalent yet.
   - Then the smoke test (#55-#57) and 1.0.

Licensing: GRAW 360 Recompiled and pgr4-native are MIT, the Xenia- and ReXGlue-derived parts
BSD-3-Clause: `port-nx/src/d3d12/README.md`, `NOTICE` (4) and `LICENSES/PROVENANCE.md`.
