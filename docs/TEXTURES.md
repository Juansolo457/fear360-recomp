# Texture overhaul: PC textures in the 360 game

_Started 2026-10-21 after play-test 1 ("textures are very, very low res"). Working since 2026-10-22._

When the 360 game creates one of its textures, the renderer draws the PC version's copy instead,
at the PC's resolution (almost always 2× per side). The pack is built locally from the player's
own PC install and never distributed. Off unless a pack is given; any texture the pack doesn't
cover, or whose file fails to load, stays the 360's.

## Status
| | |
|---|---|
| ✅ Offline pack builder (`tools/texture_pack.py` + `fear_texindex`) | reads both archives, matches by path, quality gate, writes the pack |
| ✅ Runtime replacement (`port-nx/src/texture_pack.*`, hooks in `native_textures.cpp`) | streamed from the pack on the texture worker; per-key fallback |
| ✅ Keys agree offline and at runtime | checked with `fear_texture_log_keys` on the intro: 393 catalogue textures found in the offline index; the only misses besides video frames were two 4×4 textures |
| ✅ Visible in game | A/B at 2×: the ATC guard's shoulder patch and fabric weave go from blurred to sharp |
| ✅ Pack v2: normal maps, RGBA and mixed formats | 4,710 replacements (v1: 2,666 DXT-only); in-game check of v2 below |
| ⏳ Not covered | cube/volume textures (85), 360 textures with packed mips at the base (263, all tiny), 360-only content (192), same-size pairs (972) |

**Coverage.** Of the 5,105 360 textures that have a larger PC original, the pack replaces 4,710
(92%); 78 fail the quality gate (genuinely different pictures, e.g. the Dell laptop) and the
rest are duplicates or have no usable PC file. In the intro, 393 distinct catalogue textures
were used; 181 were replaced by v1 and 157 more were the formats v2 adds. The other ~1,800
"new textures" the renderer counts in the intro are video frames (the Bink planes, `k_8`), which
never match the index, as intended.

## Build a pack
```powershell
python -I tools/texture_pack.py bundle  <Game>/uberCatalog.cat private/texwork
port-nx/out/build/win-amd64-relwithdebinfo/fear_texindex.exe private/texwork/x360.bundle private/texwork/x360.tsv private/texwork/x360_base.bin
python -I tools/texture_pack.py build   private/texwork "<PC F.E.A.R. install>" private/texture_pack
pwsh -NoProfile -File tools/launch.ps1 -Nx -TexturePack private/texture_pack -Extra '--nx_renderer native'
```
About 6 minutes and 1.3 GB. `report.tsv` in the pack says what happened to every 360 texture;
`compare <work> <run.log> --pack <pack>` breaks down a run logged with
`--fear_texture_log_keys true`.

## How it works
- **Key.** XXH3 of the 360 texture's base level exactly as it sits in guest memory (tiled,
  big-endian), seeded with format, size, tiling and pitch. The runtime computes it when the
  texture cache sees new content; `fear_texindex` computes it offline with the SDK's own
  `GetGuestTextureLayout`, from the `.tex` payload's fetch constant. One function
  (`fear::texture_pack::ReplacementKey`) serves both.
- **Index.** `index.tsv`: key, VkFormat, width, height, mips, data offset, file, and an optional
  host swizzle. The DDS files hold full mip chains.
- **Runtime.** On a hit the texture cache creates the image at the replacement's size and
  format, streams the DDS in on its worker thread (the draw sees "not ready" for a frame or
  two, as for any streamed texture), and never refills it in place. A failed read marks the key
  bad and the 360 texture is used.
- **Quality gate.** The PC base level, box-filtered per channel to the 360 size, against the
  decoded 360 base level, in the 360's logical channel order: PSNR ≥ 24 dB, or for normal maps
  a channel correlation ≥ 0.9 (the 360's normal maps were re-derived and differ in strength).

## Format plan
All 360 textures are fetched unsigned (no signed or gamma formats in the headers), and every
`k_8_8_8_8` one is swizzled `ZYXW`: its texels are B, G, R, A in memory, like the PC's
uncompressed DDS.

| 360 format | PC file | Pack | Count |
|---|---|---|---|
| DXT1 / DXT2_3 / DXT4_5 | DXT1/3/5 (same or different) | PC blocks as they are | 2,762 |
| `k_8_8_8_8` | uncompressed BGRA | PC texels as they are (RGBA8 image, same swizzle as the 360's) | 428 |
| `k_8_8_8_8` | DXT1/3/5 | PC blocks, with a host swizzle override (`60A`: guest X/blue reads host B, Z/red host R) | 626 |
| DXN (two-channel normal maps) | uncompressed or DXT | PC X and Y re-encoded to BC5 (min/max endpoints; 43–49 dB round trip), mips box-filtered | 894 |

The channel mapping was measured, not assumed: on samples, PC R/G matched the 360's DXN R/G,
and PC RGBA matched the 360 `k_8_8_8_8` after the B/R swap.

## Formats
- **360 catalogue** (`uberCatalog.cat`, big-endian): 32-byte header, 16-byte entries (name
  offset, data offset, size, compressed size), zlib payloads (stored when the compressed size
  is 0 or not smaller). A `.tex` payload is a 0x34-byte header with the Xenos fetch constant at
  0x1C, then the data in the GPU's tiled layout.
- **PC archive** (`FEAR*.Arch00`, LithTech `LTAR` v3, little-endian): 48-byte header, the name
  table, 32-byte file entries (name offset, offset, stored size, size, flags), 16-byte folder
  entries (full path, first subfolder, next sibling, file count); files are grouped in folder
  order and stored uncompressed. Textures are plain DDS.

## Next
- Hands-on play with v2 (normal maps change the lighting most).
- BC7 for the 428 RGBA8 replacements (a quarter of the memory, same look).
- Cube maps and the tiny packed-mip textures, if they turn out to matter.
- Optional: a local AI upscale of the 360-only and same-size textures, kept separate from the
  PC-sourced pack.
- *F.E.A.R. Files*: the PC expansions' archives (`FEARXP`, `FEARXP2`) feed the same tool.

Nothing from either version goes in the repository: the pack, the bundle and the reports live
in the gitignored `private/`.
