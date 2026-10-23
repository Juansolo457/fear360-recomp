# Handoff: Steam Deck performance (reviewed; next is the build and a Deck run)

Written 2026-10-22 for a second opinion on the Deck performance work, **reviewed the same evening
(Fable, adversarial review; its findings are on #28)**, and updated for whoever continues. The user's
standing rules: review the code and the plan, change what is wrong, and **don't build or launch the
game without asking the user**. Tracking issue: GitHub
[#28](https://github.com/Juansolo457/fear360-recomp/issues/28) (all the numbers below are in its
comments, too). General project state: `docs/HANDOFF.md`, `docs/PROGRESS.md`, `docs/REVIEW.md`.

## Where it stands (read this first)
- Commits since the review was asked for: `db180a0` (the three Deck changes below, **not built,
  not run**) and `b1035eb` (the review's fix, see "Review findings"). Both pushed to `origin/master`.
- **Next step: build both ports and run the Deck package**, with the user's go-ahead for each. The
  build folders are mid-way through a reconfigure to `-march=x86-64-v3`; the next build finishes
  it (full recompile, SDK included) and picks up `-ffp-contract=off`.
- Before that build it is worth adding the profiler attribution in finding 2 below, so the same
  Deck run that tests the FMA change also proves (or refutes) its diagnosis.

## Overnight analysis (2026-10-23): `docs/ARCHITECTURE.md`
Static analysis only, nothing built or run. The main-thread `ZwCreateEvent`/`ZwDeviceIoControlFile`/
`ZwClose` trio matches the Deck controller's rumble: SDL's Steam Deck driver sends each change as a
synchronous HID feature report on the game thread, and Wine's `HidD_SetFeature` creates and closes
an event per report. **Test first, no build: vibration off in the game's options.** The hottest
main-thread function, `sub_82973C88`, is the 360 Direct3D's spin loop waiting for the GPU (fence,
ring space, swap throttle): 24% of the thread at 60 fps (idle time burnt as power), about 1% in the
slow fights. Those are CPU-bound: 19 ms of game-thread work per frame (median), with the renderer
thread at 4.6 ms and the GPU at 13 ms. The doc ranks the options (rumble thread, per-module profile,
`[rexcrt]`, the translator's register options, a native Direct3D layer) and maps the executable. Filed as #29 (rumble), #30 (profiler), #31 (`[rexcrt]`), #32 (Direct3D spin), #33 (SDK
spinning), #34 (register options) and #35 (native Direct3D research).

## Deck result of 7d3a36c (`fear_007.log`, 2026-10-22)
Fights at 1,000–2,500 draws went from 52.6 to **56.7 fps** (77% → 90% of frames on time); all
gameplay 56.6 → 58.3 fps (92% → 95%); total CPU 2.1–3.5 → 1.5–2.1 cores. ucrtbase is gone from the
main thread (the `fma` diagnosis holds) and the `WaitForVBlank` spinners are gone. The main thread
(~89% of a core) is still the limit: ~15% of it in Wine system calls (`ZwDeviceIoControlFile`
8–10%, `ZwCreateEvent`, `ZwClose`, `ZwWaitForAlertByThreadId`), the rest game code spread thinly.
Also: the SDK's `TimerQueue` spin-waits (~14% of a core, disruptorplus `spin_wait_strategy`),
Wine's message thread 332 polls (~25%). **The user still sees Deck-only graphics glitches**
(tracers, corruption; same build clean on the PC); video to come. Next steps are on #28.

## After the review (2026-10-22): done before the build
- **Finding 2:** the profiler names samples outside the translated code by the nearest named
  export at or below them, from each module's PE export table (`ucrtbase.dll!fma`,
  `ntdll.dll!NtYieldExecution`, ...; Wine's builtin DLLs are PE files too). Code outside any module
  (Wine's Unix side) stays "(unknown)".
- **Finding 3:** the profiler's thread was in fact named and never sampled; it now logs its own CPU
  use (`[profile] the profiler itself`; part of its cost under Wine is in the wineserver process,
  which no thread here shows), and threads nobody named are labelled with their start function
  (`thread 404 (from dxgi.dll!...)`).
- **Also fixed:** the sampler named and counted a sample while the thread was still suspended
  (`GetModuleHandleExW` takes the loader lock, the map insert the heap lock): a possible deadlock if
  the suspended thread held either. It now resumes the thread first.
- **Finding 5:** `port-nx/src/cpu_check.cpp`, built for x86-64-v2 without the PCH (per-file options
  in both CMakeLists), checks every x86-64-v3 feature with `cpuid` (and XCR0 for the AVX state) from
  a `.CRT$XCB` initializer, before the exe's own static initializers, and shows a message box and
  exits on an older CPU. The SDK's DLL initializes before it, which no check in the exe can cover.

## Review findings (2026-10-22, Fable)
1. **Fixed (`b1035eb`): the ports' own code built without the SDK's `-ffp-model=strict`.** The SDK
   sets it with `add_compile_options` in its own directory
   (`third_party/rexglue-sdk-nx/CMakeLists.txt`), which never reached `fear_recomp` (the translated
   code), `src/native/*.cpp` or the PCH: `build.ninja` had `-march=x86-64-v3 -O2` and nothing else for
   them. Harmless under v2; under v3 clang's default `-ffp-contract=on` fuses `a*b+c` written in one
   expression into an FMA (one rounding where the PowerPC had two). Both `port-nx/CMakeLists.txt` and
   `port-files/CMakeLists.txt` now add `-ffp-contract=off` at directory scope, before
   `include(generated/rexglue.cmake)`. Verified with `.tools/llvm` clang
   (`--target=x86_64-pc-windows-msvc -O2`): with the flag `std::fma` is still one `vfmadd213sd` and
   `a*b+c` stays `vmulsd`+`vaddsd`; without it both fuse. Not `-ffp-model=strict`: that also forces
   strict FP-exception semantics across the whole translated image. The generated code itself is
   probably unaffected (one statement per instruction; SIMDe's `add(mul())` crosses function
   boundaries, which frontend contraction does not fuse), so this mostly protects the renderer.
2. **The `fma` diagnosis is unproven.** The profiler buckets by module only
   (`port-nx/src/profiler.cpp`, `Bucket()`), so "ucrtbase" could be `fma`, `memcpy`/`memset` (the
   SDK's HLE kernel and the renderer call the CRT's), `vsnprintf`, `qsort`... The main thread's
   ucrtbase share in `fear_006.log` is 0% / 9% / 17% in three consecutive windows, and there is **no
   PC profile of the main thread** to compare against. Cheap and worth doing before the build: walk
   `ucrtbase.dll`'s export table (nearest export at or below the IP; no dbghelp) and report
   `ucrtbase!fma` etc. The same attribution answers the main thread's 11-14% in ntdll (open item 4).
3. **Unnamed threads.** The profiler samples from its own unnamed `std::thread`
   (`profiler.cpp`, near the end), and under Wine `SuspendThread`/`GetThreadContext` at 200 Hz on
   ~8 threads are server round-trips, so one of "thread 404 / 492 / 332" is probably it: name it.
   Thread 404 has our vsync thread's exact mix (ntdll 57%, unknown 32%, dxgi 11%), so it is probably
   Wine's DXGI vblank worker; the next run tells (it should vanish now that we never call
   `WaitForVBlank`). **Thread 332 (~15% of a core, 87% in win32u.dll) is unexplained** and was not in
   the earlier analysis: a message pump spinning in `NtUserPeekMessage`?
4. **The ring's 200 us sleep will be ~1 ms on the Deck.** `rex::thread::SleepUntil` uses a waitable
   timer; under Proton timers are wineserver-signalled and its timeout loop is millisecond-granular
   even with fsync. The Deck logs show **exactly 1.0 wait a frame, 4-14 ms long**, right before the
   present, so the wake-up slip adds to present latency (with mailbox at a 120 Hz refresh, a 0.5 ms
   average slip now and then crosses a refresh). Probably still a net win over the hyperthread
   contention, but check the `[native] per frame: ... waits for the game, ... ms waiting` and
   `frames shown N refreshes` report lines before and after. The real fix is an event the game's
   fence write can signal instead of polling.
5. **x86-64-v3 is a silent hard requirement for PC users.** No AVX2 (pre-Haswell; Pentium/Celeron
   before 2017) crashes in the SDK's static initialisers before any log line. If the PC release
   stays v3, `src/main.cpp` needs `IsProcessorFeaturePresent(PF_AVX2_INSTRUCTIONS_AVAILABLE)` with a
   message box first thing. `-msse4.1` in the recomp options is now redundant.
6. Checked and fine: `UnderWine()` -> `DisplayVblankLoop` returns false -> `VblankLoop`'s timer
   fallback at `display_hz x guest_vblanks_per_refresh` with `DisplayRefresh()` every `per_refresh`
   vblanks and `PaceToDisplay` holding to `refreshes_per_frame`; `WatchDisplay` (the other
   `WaitForVBlank` user) only starts when `!display_pacing_`, so nothing spins in DXGI under Wine;
   `Configure()`'s one-shot `Waiter::Open()` is fine. The vblank timer jitters by the same ~1 ms
   under Wine; the catch-up loop handles it.
7. Perspective: items 1-3 of the plan below are unlikely on their own to take a 19-24 ms fight frame
   under 16.7 ms. The headroom for 60 fps in fights is in open items 5 (codegen register locals) and
   6 (3 us a draw on the Deck vs 0.3 on the PC). Don't read the next build as "the fix".

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

## What was changed in `db180a0` (**not built, not run**) and what the review said about each
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
   `add(mul(...))`) and the SDK may be affected. **Reviewed: (b) was real for the ports' own code
   and is fixed in `b1035eb` (finding 1); (a) confirmed by compiling a test, still disassemble a
   built function to be sure; the diagnosis itself is finding 2.** (c) the SDK's own code
   assumptions under v3 (its presets use v2; the comment in `build-nx.ps1` says its byte-swaps
   need SSSE3; its mac preset already uses v3).
2. **Two threads spin in `WaitForVBlank` under Proton.** **Change:**
   `display_vblank::UnderWine()` (Wine's ntdll exports `wine_get_version`);
   `NativeGraphicsSystem::DisplayVblankLoop` (`native_system.cpp`) returns false under Wine, so the
   existing timer fallback in `VblankLoop` runs the same plan (guest refresh = display Hz x
   guest vblanks per refresh, a "display refresh" every N timer vblanks for `PaceToDisplay`).
   *Review:* the timer drifts against the real display (gamescope composites with mailbox), so
   expect an occasional frame shown a refresh early/late; is there a better vblank source under
   gamescope (e.g. Vulkan `VK_KHR_present_wait`/present timing, or letting FIFO pace)? Is
   "thread 404" really DXGI's, and does it stop? **Reviewed: the fallback path is correct
   (finding 6); thread 404 is finding 3.**
3. **The ring thread spun while waiting for the game.** Its WAIT_REG_MEM loop yielded 64 times,
   then called `rex::thread::Sleep(50 us)`, which the SDK turns into a yield for anything under
   100 us (`third_party/rexglue-sdk-nx/src/core/threading_win.cpp`), so it never slept.
   **Change:** 16 yields, then `rex::thread::SleepUntil(now + 200 us)` (a high-resolution
   waitable timer). *Review:* latency cost on the PC (the ring waits ~1x a frame); whether Wine
   honours `CREATE_WAITABLE_TIMER_HIGH_RESOLUTION` (if not, `SleepUntil` falls back to
   `Sleep(us)` → 1 ms); whether an event the game signals could replace polling. **Reviewed:
   expect ~1 ms under Proton regardless of the flag, and it is on the present's critical path
   (finding 4); measure with the per-frame wait report.**

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
