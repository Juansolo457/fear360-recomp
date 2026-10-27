# The native Linux and Steam Deck build: the plan (parked)

Parked on 2026-10-27, on branch `linux-native` (off `vulkan-linux`, PR #78), until the user picks
it up again. The goal: F.E.A.R. and F.E.A.R. Files as native Linux programs on the Vulkan backend
([VULKAN_RENDERER.md](VULKAN_RENDERER.md)), and no Proton for Linux or the Steam Deck at all.

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
