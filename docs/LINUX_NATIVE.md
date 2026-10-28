# The native Linux and Steam Deck build

Started 2026-10-27 on branch `linux-native`. The goal: F.E.A.R. and F.E.A.R. Files as native
Linux programs on the Vulkan backend ([VULKAN_RENDERER.md](VULKAN_RENDERER.md)), and no Proton for
Linux or the Steam Deck at all. Linux draws with Vulkan only; there is no renderer choice there.

## Status
- Phases 1 to 4 done: both games build in the Deck container (`tools/build-deck.sh`; glibc 2.35
  and `GLIBCXX_3.4.32` at most) after SDK patch 0022 (the fear-nx patch's chrono shims clashed
  with libstdc++ 13). The panels read SDL3's gamepads on Linux (`fear_panel.cpp`).
- WSL run (llvmpipe), from the package unpacked over a copy of the game data on ext4 (a
  case-sensitive file system), started by `fear-linux.sh`: the collection menu, picking F.E.A.R.
  Files (the program switch Switch Game uses), F.E.A.R. through its intro and briefing, F.E.A.R.
  Files into gameplay with the HUD and gun. Errors in the logs: the same ones as on Windows.
  `tools/autoplay-linux.sh` is the unattended run.
- The first Deck test (2026-10-27): no controller in the game. The panels had opened SDL's
  gamepad subsystem before the runtime, so SDL announced the controllers to them only and the
  SDK's driver never saw one. The collection menu now releases its gamepads before the game
  starts (`fear::panel::ReleaseControllers`), and the panels latch SDL's button presses (a tap
  between frames, or a pad opened mid-press). Checked in WSL with a virtual Xbox 360 pad (uinput):
  the menu, then the game past Press Start, in both games.
- The second Deck test: SIGBUS in a guest memcpy (F.E.A.R. Files loading a level, after
  settings changes in both games). Each run left its guest memory named in `/dev/shm` (the
  title's hard exit never reaches the SDK's unlink), and a few restarts filled that tmpfs. SDK
  patch 0023 unlinks it once created; checked in WSL (a killed run leaves nothing behind).
- Frame rate: PC Settings' Frame rate (120 by default), with mailbox presentation. With no
  display vblank to wait on, `display_vblank::Configure` runs the timer at twice the setting. The
  first launcher pinned the timer at 240 Hz on the command line, and Save and restart drops those
  flags (they would override the setting), so every frame rate chosen came back at the SDK's
  60 Hz timer: 30 fps (third Deck test). `FEAR_FPS` now passes `--fear_frame_rate_limit`.
- Render thread speed: fights ran about 15 µs per draw on the Deck (1.5 on the PC), 50-60 fps at
  800-1,200 draws. `perf` on the Deck (4 captures in fights): 48-51% of the render thread was
  `librenderdoc.so`. SteamOS installs RenderDoc system-wide, and the SDK's plain `dlopen` loaded
  it into every run, hooking every Vulkan call. SDK patch 0024 (from GRAW's #51 fix) uses it only
  when it's already in the process. Next in the profile: descriptor set allocation per draw
  (~9%) and texture-cache lookups (~5%).
- Game thread: with RenderDoc gone, the heaviest fights (1,000+ draws) were bound by the game's
  main thread (a full core: ~69% translated game code, ~17% our hooks, mostly the vertex
  mirror's page hashing; render thread ~33%, GPU under half busy). RelWithDebInfo compiles in the
  SDK's Tracy zones and perf counters (an atomic add per guest indirect call) and builds the
  translated code at -O2; the Deck build is now Release (-O3) with `-g` kept: 1,000-1,200-draw
  frames went from 18.3 to 15.1 ms median. The state tracker decodes whole packets straight
  from guest memory (same result; too small to see in the numbers).
- Fourth Deck test (2026-10-28, frame CSVs on; Interval 4, Rooftop and Sprint Instant Action):
  F.E.A.R. at 60 fps held 60 (p99 18 ms, 0.1-0.2% of frames over 33 ms); F.E.A.R. Files at 120
  ran a median 8.7 ms, with its big fights (1,000-2,000 draws) at 18.3 ms. There the vertex
  mirror hashes about 8,800 pages a frame (2.7 ms of hooks). Hashing beats watching per page on
  the Deck (XXH3 of 4 KB: 0.14 µs; a write fault: 7-9 µs, plus 4-6.5 µs to re-protect), so the
  cost is the volume: each draw asks for its whole vertex buffer. Pipeline builds hitch first
  visits only (the cache works). The remaining hitches are mostly game-thread time the renderer's
  counters don't cover. Two docked display drop-outs: the kernel logged a DisplayPort link
  failure first (the TV newly at 120 Hz over the dock), not a GPU fault.
- The game thread (2026-10-28, PR #88; Sprint with the 360's settings, 120 fps, field of view 90,
  bloom and MSAA off):
  - **Vertex ranges from the index bounds** (`--fear_vertex_index_range`, docked): 1,000+ draw
    frames 18.3 to 12.6 ms median, hashed pages 8,765 to 541 a frame, frames under 60 fps 5.4%
    to 2.2%. A profile of those fights then showed the main thread at a full core, about 85% of
    it translated game code (the hottest single function: the game's own GPU wait spin), with
    the GPU about 55% busy.
  - **Register options stage C** (#34): no clear change in the big fights.
  - **ThinLTO** (#87), against stage C alone, both handheld at the screen's 90 Hz: frames
    slower than 90 fps 23.7% to 18.4%, over 17 ms 2.8% to 1.9%, hitches over 33 ms 0.59% to
    0.29%; 1,000-1,400 draws p90 15.6 to 13.4 ms. Now the default (`FEAR_LTO=OFF` turns it
    off). lld 20 left the translated code's per-function `.ltext.*` sections apart (100,000+,
    past the ELF section index fields, so binutils' `objdump` failed); `tools/linux/ltext.ld`
    merges them. A docked 120 Hz retest is #89.
- The package: `python3 -I tools/steamdeck/make_package.py --linux` (on Linux), an update zip over
  the Proton package's folder: `fear-linux.sh`, the programs and their libraries,
  `README-SteamDeck-Linux.txt`.

## Where it starts
- `tools/setup-sdk.sh`, `tools/build-linux.sh` and `tools/build-deck.sh` (podman, Ubuntu 22.04,
  glibc 2.35) are written but have never run.
- WSL (Ubuntu, clang 21, podman) has what GRAW's native Deck builds used. A clone on WSL's own
  file system is needed: a Windows checkout's CRLF breaks the SDK's FFmpeg build (GRAW's
  SETUP.md). A first clone is at `~/fear360-recomp` there, with `port-nx/assets` and
  `port-files/assets` linked to the Windows disc folders; its SDK setup was stopped partway
  (`tools/setup-sdk.sh --force` redoes it).
- `tools/steamdeck/make_package.py` is all Proton: the Windows programs, the Visual C++ runtime,
  `WINEDLLOVERRIDES` launch options.

## Gaps in the game's own code on Linux
- **Resolved: the panels' controller input** (`fear_panel.cpp`). It was XInput only, so the
  collection menu and PC Settings got no controller on Linux; the SDK's `InputSystem::GetState`
  couldn't stand in, because the game's active callback (`fear_app.h`) turns the SDL driver's
  state off while a panel is open. The panels now read SDL3's gamepads directly (Status, above).
- Frame pacing (`display_vblank.cpp`): a timer only on Linux, at twice the frame-rate setting
  (Status). GRAW never solved Vulkan pacing on Linux either (`VK_KHR_present_wait`).
- No Linux versions, acceptable at first: the start-up CPU check, the crash report, the sampling
  profiler.

## Phases
1. **WSL build:** `setup-sdk.sh`, then `build-linux.sh` for both games; fix what doesn't build;
   port the panel input.
2. **WSL run** (llvmpipe: correctness, not speed): boot, menus and PC Settings, the collection's
   switch between the two games (`posix_spawn`), saves and settings, paths on a case-sensitive
   file system (the disc folders through `/mnt/c` are case-insensitive: test from a Linux copy),
   audio. The autopilot (`autoplay.ps1`) needs a Linux counterpart.
3. **Deck build:** `build-deck.sh` for both games; its glibc and libstdc++ check.
4. **Native Deck package:** each game's Linux program, `librexruntime` and `libTracyClient`, the
   disc data, `user/` with the Deck settings (no MSAA, the 360's textures, 60 fps), a `launch.sh`
   (paths, the collection file, a log), `collection.toml` naming the Linux programs; added to Steam
   as a non-Steam game with no compatibility tool. A programs-only update zip too. A new README.
5. **The user's Deck tests:** boot and the controller in the collection menu; switching games;
   the guns (exact depth bias on the Deck's 32-bit float depth: the offset's size follows the
   depth; a fixed one needs `leastRepresentableValueForceUnormRepresentation`, an SDK patch);
   shimmer (GRAW's native Linux build had it); audio, saves, suspend and resume; 60 fps held
   (frame CSV); then 120.
6. **Proton retired:** its parts out of `make_package.py` (keep `--windows` for other Windows
   PCs), the README and SETUP.md.
