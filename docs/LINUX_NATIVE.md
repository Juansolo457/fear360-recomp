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
- Frame rate: the launcher runs at 120 fps by default (`FEAR_FPS=60` for 60), with mailbox
  presentation.
- Not tested yet: audio, the Deck itself.
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
- **The panels' controller input** (`fear_panel.cpp`): XInput only, so the collection menu and
  PC Settings get no controller on Linux. The SDK's `InputSystem::GetState` can't stand in as is:
  while a panel is open the game's active callback (`fear_app.h`) turns the SDL driver's state off
  (`sdl_input_driver.cpp`, `is_active()`). Either read SDL3's gamepads directly on Linux, or give
  the panels a path past the active check.
- Frame pacing (`display_vblank.cpp`): timer only on Linux. GRAW never solved Vulkan pacing on
  Linux either (`VK_KHR_present_wait`).
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
