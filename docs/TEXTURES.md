# Texture overhaul plan: PC textures in the 360 game

_Drafted 2026-10-21, after play-test 1 ("textures are very, very low res")._

Goal: when the 360 game uses one of its textures, the renderer draws the PC version's higher
resolution copy instead. Built locally from the player's own PC install, never distributed.
Toggleable, with automatic fallback to the 360 texture.

## What the survey found
`tools/texture_names.py` and `tools/texture_compare.py` (read-only, CSV in `private/`):

| | |
|---|---|
| 360 textures (`Game/uberCatalog.cat`, `.tex`) | 6,251, about 793 MB uncompressed |
| PC textures (Ultimate Shooter Edition `FEAR*.Arch00`, `.dds`) | 6,288 |
| Same file name on both | **6,069 (97%)**; the 360 even keeps the PC folder paths (`Prefabs\Industrial\Interactive\AC01_D`) |
| **PC copy is larger** | **5,105**: 5,081 at 4× the pixels (2× each side), a few at 8–1024× |
| Same size | 963 (no gain; leave them) |
| PC smaller | 1 |
| 360-only (keep 360 texture) | ~180: controller prompts, autoshot UI, laser sight, Dell product placement, Instant Action and bonus content |
| 360 formats | 8_8_8_8 1,624 · DXT1 1,622 · DXT2/3 1,136 · DXT4/5 948 · **DXN 921** (two-channel normal maps) |
| Typical sizes | 360 256² / 128² / 512²; PC 512² / 256² / 1024² |

So the 360 halved nearly every texture, and the PC has the originals under the same names.

## The two formats
- **360 catalogue** (`uberCatalog.cat`, big-endian): 32-byte header (version 3, header size,
  9,888 entries, name-table offset/size, data offset), then 16-byte entries (name offset, data
  offset, size, compressed size); every payload is a **zlib** stream. A `.tex` payload is a
  0x34-byte header followed by the texture data. The header holds the **Xenos texture fetch
  constant** at 0x1C (format, size, dimension, endianness; base/mip addresses zero until
  loaded), so the data is in the exact tiled layout the GPU reads.
- **PC archive** (`FEAR*.Arch00`, LithTech `LTAR` v3, little-endian): 48-byte header (magic,
  version, name-table size, 247 folders, file count…), the name table, then 32-byte file
  entries (name offset, offset u64, stored size u64, size u64, flags), stored uncompressed.
  Textures are plain DDS (DXT1/3/5 or uncompressed RGBA, with mips).

## How replacement works
The renderer can recognise a texture by content, without knowing its name:
1. **Offline**, for every matched pair: hash the 360 texture's base-level bytes exactly as they
   sit in guest memory (straight from the `.tex` payload), and record `hash → PC file`.
2. **At runtime**, fear-nx's texture cache already hashes each texture's guest data when it
   creates or refreshes the image (`texture_hash` / `content_hash` in
   `port-nx/src/native/native_textures.cpp`). Add a replacement key (the same base-level hash,
   computed once per new content) and, if it is in the index, build the Vulkan image from the
   PC DDS instead of untiling guest memory. Everything downstream (views, swizzles, samplers,
   the resolution-scaled sampling) stays as it is: shaders sample normalised coordinates, so a
   larger image just means more detail.

No reverse engineering of the game's loader is needed, and textures the game rewrites at
runtime (videos, render-to-texture) never match the index, so they are never replaced.

## Phases
### Phase 1: readers and an offline index (Python, `tools/texture_pack.py`)
- Read both archives (the probes in `texture_compare.py` are the starting point).
- Decode 360 textures: Xenos untiling (port `texture_util::GetTiledOffset2D` from the SDK) and
  endian swap (fetch constant endianness: k8in16 etc.), then BC/RGBA decode for comparison.
- **Match by full path first, then by name**; flag duplicates (same name, different folder).
- **Quality gate:** downscale each PC texture to the 360 size and compare it to the decoded 360
  texture (PSNR / SSIM per channel). That catches pairs that aren't the same picture, and
  channel-layout differences (normal maps, packed specular). Only passing pairs go in the
  pack; the report lists the rest.
- **Format plan per 360 format**, so the replacement means what the shader expects:
  - DXT1/DXT2_3/DXT4_5 → keep the PC's BC1/2/3 (same channel meaning).
  - **DXN (normal maps)** → the PC's are uncompressed RGBA: convert to BC5 with the 360's channel
    order (verified by the quality gate), or ship RGBA with a matching view swizzle.
  - 8_8_8_8 → RGBA8 (or BC7 later to save VRAM).
  - Gamma (`_GAMMA`/sRGB) formats and signed formats follow the 360 fetch constant.
- Output to a local pack (`<user_data_root>/texture_pack/` or `private/texture_pack/`): one DDS
  per replacement with full mips, plus `index.json` (hash → file, size, format, swizzle,
  source names). Rebuilt by the user; gitignored.

### Phase 2: runtime replacement (`port-nx/src/native/native_textures.cpp`)
- cvars: `fear_texture_pack` (path; empty = off) and `fear_texture_pack_report`.
- Load `index.json` at startup; DDS files load lazily on the existing streaming worker.
- On image creation/refresh: compute the replacement key for 2D textures whose format is in
  the index, look it up, and create the image at the PC size with the PC mips. Cache
  "no match" per content hash so the lookup costs nothing in steady state.
- Skip resolve destinations, cube/3D textures (first pass), and anything whose content changes
  per frame.
- Stats in the log: replaced / looked up / no match / failed loads; optional dump of unmatched
  hashes with their size and format, to find gaps.

### Phase 3: validation
- `tools/smoke.ps1` screenshots with the pack on and off at the same spots (title, the first
  rooms, Instant Action); then a hands-on pass through several levels.
- Checklist: diffuse, normal and specular maps; alpha-tested foliage and fences; decals; skies
  and cube maps; UI and fonts (should be untouched); the 360-only content (untouched).
- VRAM and hitching: watch for stutter on level loads; the 4090's 24 GB is plenty for ~4× the
  360's texture data, but loading should stay on the worker thread.

### Phase 4: later
- BC7 for the uncompressed formats (smaller, same quality).
- Optional upscale of the ~180 360-only textures and the 963 same-size ones (an AI upscaler, run
  locally); clearly separated from the PC-sourced pack.
- *F.E.A.R. Files*: the PC Extraction Point / Perseus Mandate archives (`FEARXP`, `FEARXP2`)
  feed the same tool for the expansion discs.

## Risks and unknowns
- **Hash agreement:** the offline hash must cover exactly the bytes the renderer hashes. Phase 1
  verifies it on a handful of textures with a runtime dump of hashes before building the index.
- **Identical 360 textures** (several names, same data) that map to different PC files: pick by
  path, else leave them alone; the report lists them.
- **Different packing between versions** (channels swapped or combined, atlases): the quality
  gate catches these; such pairs stay 360.
- **Mip streaming:** if the 360 game loads mips separately, the base-level key still works; check
  in Phase 1.
- **Licensing:** the pack is derived from the player's own PC copy, on their machine. Nothing
  from either version goes in the repository.

## Effort
Phase 1 (archive readers, Xenos untile/decode, matching, quality gate, pack writer) is the bulk
of the work. Phase 2 is a contained change in the texture cache. Phase 3 needs play-testing.
