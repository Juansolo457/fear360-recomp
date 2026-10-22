# Findings: F.E.A.R. (Xbox 360)

From the user's own disc. Hashes, names and sizes only, never content.

## Dump and executable
| | |
|---|---|
| ISO | 7,834,892,288 bytes; XDVDFS game partition at `0x0FD90000`; 268 files, 3.73 GiB |
| `default.xex` | 19,251,200 bytes, SHA-256 `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792`, title `565507D9`, media `33FBCD0B`, v0.0.0.1, built 2006-09-19 |
| Original PE name | `FEAR_F.pe`; load `0x82000000`, image `0x15C0000`, entry `0x82174180` |
| XDK | 2.0.3529: D3D9/D3DX9, XACT, XUI runtime + renderer, XHV, XONLINE, XNET |
| Data | `Game/Worlds` 2.6 GB (205 files), `Game/uberCatalog.cat` 428 MB, `Game/Snd` 648 MB, `Game/videos` 270 MB of **Bink** (`.vid` files start with `BIKi`) |

## Prior art
- **[fear-nx](https://github.com/infriker/fear-nx)** (GPL-3.0, created 2026-10-20) runs this exact executable on Switch with ReXGlue and its own Vulkan renderer. Its 78 manifest function hints are in `port/fear_manifest.toml`. Its renderer contains a lot of F.E.A.R.-specific resolve handling (shared resolve aliases, tiled resolve invalidation, video planes), and its README says the same SDK source was validated on Windows.
- **Xenia Canary:** `state-playable` (D3D12, 1440p, "no issues"; [issue 628](https://github.com/xenia-canary/game-compatibility/issues/628)). Original Xenia: `gpu-drawing-corrupt`, `cpu-timing`.
- **PC Ultimate Shooter Edition** (read-only reference) at `E:\SteamLibrary\steamapps\common\FEAR Ultimate Shooter Edition`: base game plus Extraction Point and Perseus Mandate, 17 GB of `.Arch00` archives. Possible later source of higher-resolution textures.

## First Windows build (2026-10-21)
- `rexglue init` scaffold + fear-nx hints → codegen in about 2 minutes (405 files, 215 MB of C++, no warnings) → clang build of 207 steps. It links and runs. The host needs `config.gpu_plugin = "xenos"` (`port/src/fear_app.h`), or every `Vd*` call is ignored and the screen stays black.
- The game boots, plays its intro (black: the videos don't show) and reaches **Press Start** / the main menu (Campaign, New game, Continue game, button prompts), with game logic, audio, profile writes and rich presence all working.
- **Rendering bug:** behind the menu the screen shows the game's **font and button-glyph sheet**, stretched full-screen, instead of the background. It's the same at 1x and 3x, and with `--no-clear_memory_page_state`. Theory: the background is a render-to-texture or video texture whose memory once held the font sheet, and the texture cache doesn't notice the new contents (stale entry). That matches the resolve handling fear-nx had to write.
- The VFS shows a few harmless probes for files that aren't on the disc (`Interface.asset00`, `global\ui\XUI\assets.xzp`, `ShaderLibraries\`, ...), a doubled-root first attempt for `uberCatalog.cat`/`lipsEnglish.cat`, and one `snd\Lips\` lookup with no device.
- Same controller-slot issue as N3: a Logitech virtual XInput pad holds slot 0, so the Xbox pad is player 2 and the game reports "No profile selected".

## port-nx: fear-nx's renderer fixes the menus (2026-10-21)
- Same executable, same translation (codegen with ReXGlue 0.10.0 `c94f5eb` + fear-nx's patch: 405 files, no warnings). `--nx_renderer native` → correct title screen, animated menu video and text. `--nx_renderer xenos --fear_gpu_backend d3d12` on the same patched SDK → font sheet again. Captures: `private/captures/contact-nx-223327.png`.
- Their renderer: Vulkan only, renders at the guest's 1280×720 (`kOutputWidth`), honours the game's MSAA (`nx_native_msaa`), AF up to 16× (`nx_native_max_anisotropy`), async pipelines, shared tiled resolves. No draw-resolution scaling (`textures_resolution_scaled = 0`), so 4K output is upscaled 720p for now.
- **Resolution scaling added** to that renderer (`nx_native_resolution_scale`, 1–4). At 3× the log reports `Draw resolution scale: 3x (3840x2160 for a 1280x720 frame)`; the game's targets are odd guest sizes (800×720 and 520×624 with 2×/4× MSAA, 320×192 for effects) and all of them scale. Gameplay capture at 3×: clean edges, text and lighting (`private/captures/scale3-*.png`).
- **60 fps:** `--video_mode_refresh_rate 120` (launcher `-FrameRate 60`) doubles the virtual vblank the game waits two of per frame. The user reports smooth play with correct game speed at 3×. `--video_mode_refresh_rate 240` (launcher `-FrameRate 120`, now the default; 240 is the SDK's clamp) was verified the same way.
