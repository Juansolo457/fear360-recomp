# Handoff: Steam Deck performance (for a review before the next build)

Written 2026-10-22 for a second opinion (Fable) on the Deck performance work. **The user wants
this reviewed before another build is made**: review the code and the plan, change what is wrong,
and don't build or launch the game without asking the user. Tracking issue: GitHub
[#28](https://github.com/Juansolo457/fear360-recomp/issues/28) (all the numbers below are in its
comments, too). General project state: `docs/HANDOFF.md`, `docs/PROGRESS.md`, `docs/REVIEW.md`.

## The goal and the user's constraints
- **60 fps held on the Steam Deck in the biggest fights** (and 120 on the PC, which already
  holds). The user ruled out 40 fps: "this game can run over 40fps no way... if we have to we will
  rewrite large parts of this and it could take weeks". The Deck is far newer than the 360 and the
  2005 PCs that ran the PC version; the PC version runs well over 100 fps on much weaker CPUs.
- **The feel must not change.** On the Deck: "the feel is really spot on... an enhanced version of
  the 360 version". Aim assist, stick tuning, animation timing stay as they are.
- The PC version with EchoPatch is the standard (tracking issue #2; `private/pc_ref/bin/` has the
  PC's `GameServer.dll`, `GameClient.dll`, `ClientFx.fxd` from `FEARE_8.Arch00`, v1.08).

## How the Deck runs it
No Linux build: the Windows build under Proton (Experimental), Vulkan through winevulkan, the
fear-nx native renderer (`port-nx/src/native/`). Package: `tools/steamdeck/make_package.py`
(full zip ~13 GB with data, or `--programs` for a 0.18 GB update), setup in
`tools/steamdeck/README-SteamDeck.txt`. The user's Deck reports a **120 Hz** output through
gamescope (1920x1080 swapchain), so the game paced to 120 until the Deck package defaulted to a
60 fps limit (`fear_frame_rate_limit = 60` in the package's settings.toml).

## Evidence (logs in `private/deck-logs/`, git-ignored; copied from the user's USB stick)
| Log | Run | What it shows |
|---|---|---|
| `fear_001.log` | 21:14–21:18, first Deck run | paced to 120 Hz (`each frame shown for 1 refreshes`); fights 13–19 ms a frame; renderer ~4 ms CPU / 5–7 ms GPU; pipeline cache empty (first run) |
| `fear_002.log` | 21:18–21:42, 24 min, still 120 Hz pacing | median 16.7 ms a frame, worst 10 s window 31.8 ms (not analysed in detail yet) |
| `fear_003.log` | 21:59, menu only | the user set Frame rate 60 and restarted |
| `fear_004.log` | 22:00–22:02, 60 fps | light scenes hold 60; fights of ~1,000 draws: 49–57 fps, frames shown 2 or 3 refreshes; renderer waits 6–13 ms a frame for the game |
| `fear_005.log` | 22:29–22:40, 60 fps, biggest fight | 2,400–3,600 draws a frame: GPU 14–20 ms, renderer CPU ~10 ms, 30–35 fps |
| `fear_006.log` | 22:49–23:03, 60 fps, **profiler on** (200 Hz) | see the table below |

PC reference (same scenes on a fast PC, 120 fps): renderer ~1 ms CPU a frame, frames on time
99.8%+ (play-tests in `docs/PROGRESS.md`).

**Profile, heaviest windows of `fear_006.log`** (19–24 ms frames, 1,400–1,600 draws; total CPU
2.1–3.5 cores of the Deck's 8 threads, so it isn't total CPU):

| Thread | CPU | Where its samples are |
|---|---|---|
| Main XThread (F8000028), the game | ~90% of a core: **the limit** | ucrtbase.dll 15–21%, ntdll.dll 11–14%, rexruntimerd.dll 2–3%, then game code spread thinly (no guest function over 2%; top: `sub_82BCAFF0`, `sub_822A6CD0`, `sub_8224B958`, `sub_821DE698`, `sub_822D4B50`) |
| GPU VSync native (F8000018), ours | ~61% | ntdll ~57%, (unknown) ~33%, dxgi ~10%: spinning in `IDXGIOutput::WaitForVBlank` under Proton |
| thread 404 | ~61% | the same mix: apparently inside Proton's DXGI too |
| GPU ring native (F800001C), our renderer | 47–59% | ntdll 65–72% (yield loop, below), `sub_8307D8A8` 16–23% (that is our renderer's host code misattributed: see "profiler") |
| rex::thread::TimerQueue, Audio Worker, XThread0210 | 10–14% each | ntdll 75–96% |

## What is diagnosed, and what was changed (commit `db180a0`, **not built, not run**)
1. **`std::fma` is a C runtime call.** The code generator translates every PowerPC
   `fmadd`/`fmsub`/`fnmsub` (scalar) as `std::fma(...)` (see any `generated/default/*.cpp`).
   Built with `-march=x86-64-v2` (no FMA) clang can't inline it, so each is a call into
   ucrtbase, and under Proton Wine's `fma()` is a software implementation. This is the main
   suspect for the main thread's 15–21% in ucrtbase. **Change:** `tools/build-nx.ps1` builds the
   SDK and both games for `-march=x86-64-v3` (AVX2/FMA/BMI; the Deck's Zen 2 and PCs since ~2013).
   *Review:* (a) confirm the generated code's `std::fma` becomes `vfmadd` (disassemble a
   function), and that nothing else in hot paths still calls the CRT (`sqrt` under the MSVC
   target with errno semantics? `std::isnan`?); (b) **floating-point contraction**: with FMA
   available, clang's default `-ffp-contract=on` may fuse `a*b+c` written in one expression
   that the PowerPC rounded separately; the generator mostly rounds through
   `double(float(...))` per instruction, but SIMDe paths (`vmaddfp` is emitted as
   `add(mul(...))`) and the SDK may be affected. Consider `-ffp-contract=off` to keep exact
   semantics except the explicit `std::fma`; (c) the SDK's own code assumptions under v3 (its
   presets use v2; the comment in `build-nx.ps1` says its byte-swaps need SSSE3).
2. **Two threads spin in `WaitForVBlank` under Proton.** **Change:**
   `display_vblank::UnderWine()` (Wine's ntdll exports `wine_get_version`);
   `NativeGraphicsSystem::DisplayVblankLoop` (`native_system.cpp`) returns false under Wine, so the
   existing timer fallback in `VblankLoop` runs the same plan (guest refresh = display Hz x
   guest vblanks per refresh, a "display refresh" every N timer vblanks for `PaceToDisplay`).
   *Review:* the timer drifts against the real display (gamescope composites with mailbox), so
   expect an occasional frame shown a refresh early/late; is there a better vblank source under
   gamescope (e.g. Vulkan `VK_KHR_present_wait`/present timing, or letting FIFO pace)? Is
   "thread 404" really DXGI's, and does it stop?
3. **The ring thread spun while waiting for the game.** Its WAIT_REG_MEM loop yielded 64 times,
   then called `rex::thread::Sleep(50 us)`, which the SDK turns into a yield for anything under
   100 us (`third_party/rexglue-sdk-nx/src/core/threading_win.cpp`), so it never slept.
   **Change:** 16 yields, then `rex::thread::SleepUntil(now + 200 us)` (a high-resolution
   waitable timer). *Review:* latency cost on the PC (the ring waits ~1x a frame); whether Wine
   honours `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (if not, `SleepUntil` falls back to
   `Sleep(us)` → 1 ms); whether an event the game signals could replace polling.

## Open, not yet worked
4. **Main thread's ntdll share (11–14%).** Not attributed. Candidates: QueryPerformanceCounter
   (the game's timers; under Wine a syscall-ish path), critical sections
   (RtlEnterCriticalSection from guest code through the SDK), waits/yields from guest spin loops
   (KeDelayExecutionThread, Sleep(0)), heap. The profiler only names modules for non-generated
   code; record the caller (walk one stack frame, or sample a short stack) to attribute these.
5. **Code generator optimizations are all off.** `third_party/rexglue-sdk-nx/include/rex/codegen/config.h`:
   `skip_lr`, `skip_msr`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `cr_as_local`,
   `non_argument_as_local`, `non_volatile_as_local` (manifest keys in
   `src/codegen/config.cpp`), none set in `port-nx/fear_manifest.toml` or
   `port-files/fear_manifest.toml`. With them, PowerPC registers become C++ locals the compiler
   can keep in CPU registers instead of loading/storing `ctx` on every instruction (projects in
   this family, e.g. UnleashedRecomp, enable them). Probably the biggest across-the-board CPU
   win, and the riskiest: `setjmp`/`longjmp` (manifest `setjmp_address`/`longjmp_address`;
   no symbol names in the 360 image, so they'd have to be found by pattern), SEH funclets
   (`shareRegisters`), our `[[entrypoint.midasm_hook]]`s that take registers by reference
   (`FearAnim*`, `FearMenu*`, `FearAiNextUpdate`, `FearDebug*`, `FearConsoleVarDefault`), and
   anything reading LR. Measure with the profiler before/after.
6. **Renderer CPU per draw on the Deck**: ~3 µs a draw (10 ms at 3,500 draws) vs ~0.3 µs on the
   PC. `NativeGraphicsSystem::RingLoop` → `Renderer::Impl::Draw` (`native_renderer.cpp`): state
   hashing, fetch-constant resolution, descriptor sets, `fear_vertex_exact` span hashing. A
   recorder thread exists (`record_thread`). Profile the ring thread's host functions (needs
   symbols: the build's `fear.pdb` exists on the PC; under Wine, `dbghelp` may or may not read it).
7. **GPU on the Deck**: 8–20 ms a frame at 1x in fights. The game's own 2x/4x MSAA is honoured
   (`nx_native_msaa`, try `--nx_native_msaa false`); the PC texture pack (`--fear_texture_pack`)
   raises bandwidth. No Deck run without either yet.
8. **`ctx.fpscr.disableFlushModeUnconditional()`** appears in generated code between VMX and
   scalar float code: an `ldmxcsr` each time (`include/rex/ppc/context.h`, `platform/fpscr.h`).
   Cheap-ish, but worth a look if float code shows in profiles.
9. **Profiler attribution** (`port-nx/src/profiler.cpp`): a sample in `fear.exe` is put under the
   nearest translated function below it if within 1 MB, so host code linked after the generated
   code (the renderer) shows as the last guest functions (`sub_8307D8A8`). Better: use the PDB on
   the PC, or bound each function by the next one's start and treat the tail of the table as host.

## How to build, run and check (the user asks before builds and launches)
- Build: `pwsh -NoProfile -File tools/build-nx.ps1 -Project port-nx` and `-Project port-files`.
  The build folders are mid-way through a reconfigure to `-march=x86-64-v3` (a build was started
  and stopped for this review); the next build continues it (a full recompile, SDK included).
  The last complete programs are the 22:16 builds (profiler, `-march=x86-64-v2`).
- Type-check one file without building: run clang with the file's `DEFINES/INCLUDES/FLAGS` from
  `port-nx/out/build/win-amd64-relwithdebinfo/build.ninja` plus `-fsyntax-only`.
- PC run (the user plays; launch from the collection): `pwsh -NoProfile -File tools/launch.ps1
  -Collection -Extra '--nx_renderer native'`, add `--fear_profile true` for the profiler. Logs:
  `port-nx/logs/`, `port-files/logs/`; the 10-second report lines are `[native]`, the profiler's
  `[profile]`.
- Deck: `python -I tools/steamdeck/make_package.py --programs` → unpack over the install in the
  folder that *contains* `FEAR360-SteamDeck` (`/home/deck/recomps/` on the user's Deck); launch
  options in the README, plus `--fear_profile true --fear_profile_hz 200` for a profile. Logs:
  `fear/logs/fear_NNN.log` on the Deck.
- Commits so far were made with `git -c user.name=juansolo457 -c user.email=5344231+Juansolo457@users.noreply.github.com`
  and end with a `Co-Authored-By:` line; record findings on #28.
