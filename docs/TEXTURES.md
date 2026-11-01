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
| ✅ Runtime replacement (`port-nx/src/texture_pack.*`, in the D3D12 renderer's texture cache `d3d12/textures.cpp` since 2026-10-31 and the Vulkan one's `d3d12/vulkan_textures.cpp` since 1.5; fear-nx's `native_textures.cpp` before) | read from the pack on the untile workers; per-key fallback |
| ✅ Keys agree offline and at runtime | checked with `fear_texture_log_keys` on the intro: 393 catalogue textures found in the offline index; the only misses besides video frames were two 4×4 textures |
| ✅ Visible in game | A/B at 2×: the ATC guard's shoulder patch and fabric weave go from blurred to sharp |
| ✅ Pack v2: normal maps, RGBA and mixed formats | 4,710 replacements (v1: 2,666 DXT-only); in-game check of v2 below |
| ✅ *F.E.A.R. Files* pack (2026-10-22) | **7,607 replacements** of Files' 9,944 catalogue textures, from the PC expansions' archives first (`--expansions FEARXP2,FEARXP`), then the base game's; 2.0 GB. Matches include the expansions' own sets (`Prefabs/HospitalXP` 317, `Prefabs/DataCenterXP2` 212). Not packed: 1,270 where the PC copy isn't larger, 469 with packed mips at the base, 223 with no PC file (89 of them the 360's own menu art), 119 that fail the quality gate, 114 cube/volume |
| ⏳ Not covered | cube/volume textures (85), 360 textures with packed mips at the base (263, all tiny), 360-only content (192), same-size pairs (972) |

**Coverage.** Of the 5,105 360 textures that have a larger PC original, the pack replaces 4,710
(92%); 78 fail the quality gate (a few genuinely different pictures, e.g. the Dell laptop;
most are near misses at 20–24 dB, left as the 360's to be safe) and the rest are duplicates or
have no usable PC file. In the intro, 393 distinct catalogue textures
were used; 181 were replaced by v1 and 157 more were the formats v2 adds. The other ~1,800
"new textures" the renderer counts in the intro are video frames (the Bink planes, `k_8`), which
never match the index, as intended.

## Build a pack
```powershell
python -I tools/texture_pack.py bundle  <Game>/uberCatalog.cat private/texwork
port-nx/out/build/win-amd64-relwithdebinfo/fear_texindex.exe private/texwork/x360.bundle private/texwork/x360.tsv private/texwork/x360_base.bin
python -I tools/texture_pack.py build   private/texwork "<PC F.E.A.R. install>" private/texture_pack
pwsh -NoProfile -File tools/launch.ps1 -Nx -TexturePack private/texture_pack
```
`launch.ps1 -Collection` passes each game's pack from `private/` when it is built. The game saves
the folder in its `settings.toml`; Recompilation Settings > PC textures turns the pack on or off,
in both renderers (Direct3D 12 on Windows, Vulkan on Linux and the Steam Deck).
For *F.E.A.R. Files*, the same three steps over its own catalogue, with the PC expansions'
archives first (`FEARXP2`, then `FEARXP`, then the base game's; Files is the Perseus Mandate
build, carrying Extraction Point):
```powershell
python -I tools/texture_pack.py bundle  private/files/assets/Game/uberCatalog.cat private/files/texwork
port-nx/out/build/win-amd64-relwithdebinfo/fear_texindex.exe private/files/texwork/x360.bundle private/files/texwork/x360.tsv private/files/texwork/x360_base.bin
python -I tools/texture_pack.py build   private/files/texwork "<PC F.E.A.R. install>" private/files/texture_pack --expansions FEARXP2,FEARXP
pwsh -NoProfile -File tools/launch.ps1 -Files -TexturePack private/files/texture_pack
```

### From a release zip
The release zips carry the builder: `texture-pack/` (its scripts) and `fear/fear_texindex`. You
need Python 3 and your own PC copy of F.E.A.R. (its install folder, with `FEAR.Arch00`). In the
zip's top folder, after your disc is in `fear/assets`, give the builder its own Python environment
with numpy and Pillow, then run it with that environment's Python. On Windows:
```
python -m venv texwork-venv
texwork-venv\Scripts\python -m pip install numpy pillow
texwork-venv\Scripts\python -I texture-pack\texture_pack.py bundle fear\assets\Game\uberCatalog.cat texwork
fear\fear_texindex.exe texwork\x360.bundle texwork\x360.tsv texwork\x360_base.bin
texwork-venv\Scripts\python -I texture-pack\texture_pack.py build texwork "<PC F.E.A.R. install>" fear\texture_pack
```
On Linux and the Steam Deck:
```
python3 -m venv texwork-venv
texwork-venv/bin/python -m pip install numpy pillow
texwork-venv/bin/python -I texture-pack/texture_pack.py bundle fear/assets/Game/uberCatalog.cat texwork
fear/fear_texindex texwork/x360.bundle texwork/x360.tsv texwork/x360_base.bin
texwork-venv/bin/python -I texture-pack/texture_pack.py build texwork "<PC F.E.A.R. install>" fear/texture_pack
```
Then add this line to
`fear/user/settings.toml` (create the file if it isn't there yet) and start the game:
```
fear_texture_pack = "texture_pack"
```
Recompilation Settings > PC textures then turns the pack on or off. The `texwork` and
`texwork-venv` folders can be deleted afterwards.

Either way, about 6 minutes and 1.3 GB. `report.tsv` in the pack says what happened to every 360 texture;
`compare <work> <run.log> --pack <pack>` breaks down a run logged with
`--fear_texture_log_keys true`.

## Sharper text (#42)
The 360's text comes from bitmap fonts made for 720p (`interface\fonts\*.xpr`: a k_4_4_4_4
glyph atlas and an ATG font table), so at 4K every glyph is magnified about 3x and stair-steps.
`bundle` also takes the eight font atlases, and `build` redraws each at 4x from its TrueType face
(`tools/font_atlas.py`): the PC install's own (`FEARL.Arch00`: Neuropol, Typist, Digital, Lucida
Sans Unicode) and the system's Arial (Windows; Liberation Sans on Linux). The face's size,
baseline, weight and the outline in alpha are fitted to the 360 atlas, and each glyph is drawn
in its own cell at its best x, so the layout is the 360's. The controller-button glyphs and the
characters a face lacks (Neuropol has no accented capitals) stay the 360's, upscaled. About 30
seconds; `report.tsv` gives each font's fit. Only an upscale of the 360 atlas was tried first:
it kept the stair-steps. The atlases are linear in the files, but the game tiles them when it
loads them, so `fear_texindex` keys them by their tiled copy (checked against the keys a run
logged for Arial_18 and NeuropoX_36).

## How it works
- **Key.** XXH3 of the 360 texture's base level exactly as it sits in guest memory (tiled,
  big-endian), seeded with format, size, tiling and pitch. The runtime computes it when the
  texture cache sees new content; `fear_texindex` computes it offline with the SDK's own
  `GetGuestTextureLayout`, from the `.tex` payload's fetch constant. One function
  (`fear::texture_pack::ReplacementKey`) serves both.
- **Index.** `index.tsv`: key, VkFormat, width, height, mips, data offset, file, and an optional
  host swizzle. The DDS files hold full mip chains.
- **Runtime (D3D12).** Each upload of new guest content hashes the base level; on a hit the
  texture gets a resource of the replacement's size, format and mips (the 360's own host format
  when it is the same, BC5 or RGBA8, so signed fetches read it alike), and the untile worker
  reads the DDS into the upload buffer instead of untiling. The guest pages stay watched: new
  content looks the pack up again, and content it doesn't cover goes back to a guest-format
  resource (a swapped-out resource lives until the GPU is done with it). A failed read marks the
  key bad (that texture is black until its content changes) and the 360 texture is used from
  then on. Resolve destinations are never replaced. With a pack, the texture memory budget is
  four times larger (replacements are about 4x the 360's size). Holiday at 4K on an RTX 4090:
  506 of 1,618 new textures replaced (the rest mostly video frames), 120 fps.
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
