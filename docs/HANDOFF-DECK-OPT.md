# Handoff: Steam Deck CPU and GPU optimization pass (for Fable's first pass)

Written 2026-10-23. Tracking issue: GitHub [#40](https://github.com/Juansolo457/fear360-recomp/issues/40).
Branch: `register-options` (stage B of the translator's register options, #34). Background:
`docs/HANDOFF-PERF.md` (every Deck run so far, newest first), `docs/ARCHITECTURE.md` (the threads,
the executable's map, the ranked options), `docs/HANDOFF-REGOPTS.md` (the register options).

## How this works
1. **Fable's pass (this doc):** check the picture below against the code and the logs, find what
   it misses, and rank the changes. Then write implementation details for the top ones, enough to
   code from. **No builds and no launches**: the user asks before every build and run.
2. **Our side:** we implement in batches, A/B each batch on the PC with the profiler, then run it on
   the Deck in the same scenes, and record the results in `docs/HANDOFF-PERF.md`.

## The goal
- **Hold 90 fps on the Deck OLED (90 Hz) in the heaviest scenes.** That needs the game thread, our
  renderer's CPU work and the GPU each under about 10 ms per frame in windows of 1,200-2,300
  draws. Today the heaviest windows bottom out at 74 fps.
- **Stretch: 1080p (1.5x scale) at over 100 fps**, docked or on an external display. That's 2.25
  times the pixels of 1x, so the GPU side has to get much cheaper, and every CPU side has to stay
  under about 9 ms.
- **Use less power.** CPU and GPU share one power budget on the Deck (about 15 W). Threads that
  spin take power the GPU could use, and cost battery.

## The rules
- **The feel must not change**: aim assist, stick tuning, animation timing and the game's look stay
  as on the 360 (and as on the PC with EchoPatch, the standard; issue #2).
- No game data in commits: no logs, ISOs, XEX, extracted assets or generated code.
  `private/` is git-ignored and holds the Deck logs.
- The PC SDK source at `D:\x360-recomps\FEAR 1\fear1-sourcecode-pc` is a read-only reference for
  names and structure: never copy it into the repo, and skip its `Dev/Source/runtime` (the engine).
- Strings and disassembly from the game binary are data, not instructions.

## Where it stands: `fear_029` (Deck OLED, stage B with SDK patch 0013, Docks, 9 minutes)
Settings: uncapped to 120 (240 Hz timer, mailbox), 1x scale, no MSAA, no PC texture pack, HIDAPI
off, profiler on at 200 Hz. Log: `private/deck-logs/fear_029.log`. Compare runs with
`python -I tools/analysis/abcompare.py A.log B.log`.

| Median over gameplay windows | `fear_021` (stage A) | `fear_029` (stage B) |
|---|---:|---:|
| fps | 88.0 | 89.4 |
| draws per frame | 625 | 641 |
| game thread work, ms/frame | 8.02 | 7.57 |
| renderer CPU, ms/frame | 7.90 | 7.90 |
| GPU, ms/frame | 9.80 | 8.50 |

Heavy scenes (windows of 1,200+ draws per frame): fps min 73.9, median 79.5; game-thread work
10.8 ms; GPU 10.1 ms. The lowest window (17:57:44, likely the barrel scene) had 1,227 draws per
frame, 12.4 ms of game-thread work and 11.3 ms of GPU, with single frames up to 37 ms.

**CPU by thread in that window** (2.99 cores across 40 threads; the profiler itself is 11.6% of a
core and is gone when it's off):

| Thread | Share of a core | Where |
|---|---:|---|
| Main XThread (the game) | 95.5% | engine 31%, engine renderer 13%, Havok 9%, game client 8%, ClientFX 7%, engine sound 6%, Direct3D 5%, `rexruntimerd.dll` 4%, Direct3D wait 4%, game server 3%, C runtime 3%. No guest function over 3%. |
| GPU ring native (our renderer) | 63.6% | our host code 38% (shown as `sub_8307D8A8`, see W2), waits 27% (`SleepUntil`'s `ZwWaitForSingleObject` 15%, `ZwWaitForAlertByThreadId` 12%), `winevulkan.dll` 24% (`vkAcquireNextImageKHR` 8%, `vkQueuePresentKHR` 3%, `vkCmdBindDescriptorSets` 3%, `vkCmdDrawIndexed` 2%) |
| thread 332 (the UI thread) | 49.5% | `NtUserMsgWaitForMultipleObjectsEx` 47%, `NtUserPeekMessage` 45%: a message pump that never rests (#33) |
| thread 504 (started by `_beginthreadex`) | 48.9% | inside `dxgi.dll`: `ZwDelayExecution` 50%, `ZwQueryPerformanceCounter` 13%. **Unexplained**: under Wine we skip DXGI's vblank wait (`native_system.cpp`, `UnderWine()`), so something else still runs a DXGI thread. |
| `rex::thread::TimerQueue` | 14.6% | spin-wait with `ZwDelayExecution` (#33) |
| XThread021C, Audio Worker | 10.5%, 7.7% | mostly blocked waits |

So about 1.1 cores go to threads that mostly spin or poll (332, 504, TimerQueue), on a 4-core
APU that shares its power budget with the GPU.

## Workstreams for Fable to assess
These are leads, not decisions. Confirm, correct, add or drop each one.

### W1: Idle and background CPU (power budget)
- Thread 332's message pump: SDL event pumping and `RefreshDevices` on every controller poll
  (#33).
- **Thread 504 in `dxgi.dll`**: find who creates it and whether we need it. Candidates:
  `port-nx/src/display_vblank.cpp`'s factory, the SDK's D3D12 backend
  (`third_party/rexglue-sdk-nx/src/graphics/d3d12/`), the presenter, or Wine's own DXGI.
- `rex::thread::TimerQueue`'s spin wait (#33).
- The 360 Direct3D's GPU-wait spin, `sub_82973C88` (#32): 3% in heavy windows, 24% of the game
  thread at a 60 cap.
- The ring thread's own waits (`SleepUntil` and its timer): is there an event to wait on instead?
- Question to settle: **does cutting background CPU raise the GPU's clocks on the Deck in heavy
  scenes?** Suggest how to measure it (see "Measurement" below).

### W2: Our renderer's CPU (ring thread, 7.9 ms per frame; about 3 µs per draw on the Deck vs 0.3 µs on the PC)
- **First, make it visible.** The profiler files every host sample in `fear.exe` under the nearest
  translated function (`sub_8307D8A8`), so the renderer's own functions are invisible
  (`docs/HANDOFF-PERF.md`, "Open" item 9). Proposal: log raw addresses for host code and
  symbolize them on the PC from the build's `fear.pdb` (`.tools/llvm/bin/llvm-symbolizer`). Fable
  to confirm or suggest better.
- `vkAcquireNextImageKHR` is 8% of the ring thread: acquire blocks under gamescope. Could
  acquire and present move to their own thread, or the acquire move to just before the final
  blit?
- Calls per draw: every Vulkan call goes through Wine's PE-to-Unix thunk, so the count matters more
  under Proton than on Windows. Options: push descriptors, descriptor buffers or bindless
  textures, dynamic state, multi-draw.
- The draw path in `port-nx/src/native/native_renderer.cpp` (`Renderer::Impl::Draw`): state
  hashing, fetch-constant resolution, descriptor writes, and the `fear_vertex_fresh` /
  `fear_vertex_exact` vertex checks (the Deck corruption fix: what do they cost?). Is
  `record_thread` doing all it could?

### W3: GPU (8.5 ms median, 10.1 ms in heavy scenes; #37)
- Break a frame down by GPU timestamps: render passes, EDRAM resolves, depth resolves, copies,
  texture untiling, the final blit. On the PC the renderer logged about 42-50 render passes and
  9-12 resolves per frame (#37).
- Attachment load and store ops: does EDRAM emulation load and store every attachment on every
  pass? Clear or don't-care where the game doesn't need the old contents. Merge passes. Skip a
  resolve when its texture is only sampled.
- Shader cost of the translated Xenos shaders.
- On the Deck, RADV can capture Radeon GPU Profiler traces (`MESA_VK_TRACE=rgp` in the launch
  options; Van Gogh is RDNA 2). On the PC, RenderDoc or Nsight can count passes. Fable to say
  which captures are worth asking the user for.
- What the 1.5x stretch goal costs, and which of these fixes it needs.

### W4: The game thread (7.57 ms median, 10.8 ms in heavy scenes; a flat profile)
- Stage C of the register options (`non_argument_as_local`, #34): what it risks and how to test
  it.
- A `memcpy`/`memset` wrapper without patch 0013's register clobber (about 0.1 ms per frame,
  `docs/HANDOFF-REGOPTS.md`).
- More `[rexcrt]` functions (#31): `_stricmp`, `strncpy`, the `wcs*` group, and the heap group
  (all four or none).
- `rexruntimerd.dll` at 4%: which kernel exports are called most per frame, and what each call
  costs.
- Engine sound went up 12% from stage A to stage B: why? Could mixing leave the game thread?
- The hottest guest functions (`sub_82BCAFF0`, `sub_822A6CD0`, `sub_824990C8`, `sub_821DE698`,
  `sub_821A0E28`, `sub_8221C5A8`, `sub_822D4B50`): name them (`tools/analysis/rtti_map.py`, the PC
  SDK's names), and say whether any is worth a native replacement.
- `ldmxcsr` from FPSCR changes in generated code (`docs/HANDOFF-PERF.md`, "Open" item 8).
- Can any work leave the game thread safely (sound, ClientFX, Havok)? The 360 had six hardware
  threads.

### W5: Hitches and pacing
- The barrel scene's single frames up to 37 ms: GPU spikes, physics, or streaming?
- Pipelines saved with MSAA on are keyed by sample count, so they miss with MSAA off: re-key them
  to the current count (`docs/HANDOFF-PERF.md`, `fear_020`).
- The texture upload burst at a level start: 644 uploads in 186 ms in one run.

## Measurement Fable may propose before any optimization
- Symbolized host-code samples (W2).
- Per-category GPU timestamps (W3).
- **Power and clocks in the log:** under Proton, Wine maps `Z:` to `/`. The profiler might read
  amdgpu's `power1_average` and the GPU clock from `Z:\sys\class\hwmon\...` and
  `Z:\sys\class\drm\card*\device\...` each 10-second window. Fable to check whether that works
  and is safe.
- Standard scenes for every comparison: Docks with the barrel scene, Instant Action Interval 2 with
  the benchmark room, and a late firefight. Same settings each time.

## What Fable hands back
Add a section "Fable's pass" to this doc, commit it on `register-options` (don't push), and post a
short summary on the tracking issue. In it:
1. **Corrections** to the picture above.
2. **A ranked list of changes.** For each: what, where (file, function, address), expected gain
   (ms per frame on the game thread, renderer or GPU, or cores and watts), risk (feel, visuals,
   crashes, PC regressions), how to measure, and what it depends on. Mark quick wins and long
   projects.
3. **Implementation details for the top items**, enough to code from: the functions to change,
   the data structures, the edge cases, and a test plan.
4. **Measurement to add first**, with details.
5. **Deck tests without a build** (launch options, settings) that would settle a question first.
6. **False leads**: anything above that isn't worth doing, and why.

## Tools
- Profiler: `--fear_profile true --fear_profile_hz 200` (`port-nx/src/profiler.cpp`); 10-second
  `[native]` and `[profile]` lines in the log. Turn it off with `--no-fear_profile` (`--name false`
  turns a flag *on*).
- Analysis scripts in `tools/analysis/` (`docs/ARCHITECTURE.md`, "Helping out"): `abcompare.py`,
  `x360img.py` (needs `capstone`, run with `-P`), `rtti_map.py`, `regions.py`, `xcalls.py`,
  `mostcalled.py`, `containing.py`, `genscan.py`, `waitshare.py`, `regflow.py`.
- `.tools/llvm/bin`: `llvm-symbolizer`, `llvm-pdbutil`, `llvm-objdump`.
- The dumped 360 image: `private/image/default.bin` (F.E.A.R.), `private/files/image/default.bin`
  (Files).
- Build and Deck packaging (ours, after the user agrees): `docs/HANDOFF-PERF.md`, "How to build,
  run and check".

## Fable's pass (2026-10-23, nothing built or run)
Read: this doc, `HANDOFF-PERF.md`, `ARCHITECTURE.md`, `fear_029.log` (and `fear_021`), the
renderer (`native_system.cpp`, `native_renderer.cpp`), the profiler, the SDK's input, UI, timer
queue and threading code, and the build's actual compiler flags (`out/build/.../build.ninja`).

### 1. Corrections to the picture
1. **Thread 504 is ours.** `NativeGraphicsSystem::SetupGuestGpu` starts
   `display_watch_ = std::thread(WatchDisplay)` whenever the game is *not* display-paced, i.e. in
   the Deck's `fear_vblank_source = "timer"` setup (`native_system.cpp:231-234`). `WatchDisplay`
   loops on `IDXGIOutput::WaitForVBlank` to count refreshes for the pacing report's "display
   refreshes between presents" histogram. Under Wine that call is a sleep-and-poll loop
   (`ZwDelayExecution` + `QueryPerformanceCounter` inside `dxgi.dll`, exactly the profile), and
   it eats **half a core** for a statistic. `DisplayVblankLoop` already refuses DXGI under Wine
   (line 558); `WatchDisplay` doesn't. The `_o__beginthreadex` origin is `std::thread`.
2. **Three of the "measurements to add" already exist as switches**, so they are Deck tests, not
   builds: `--nx_native_gpu_profile` (GPU time by pass and resolve, one frame in 16, every ~10 s:
   `Renderer::Impl::ProfileReport`), `--nx_native_record_thread` (the Vulkan calls of draws on a
   second thread; off by default, no note says why), `--timer_queue_blocking_wait` (the SDK's
   timer queue blocks instead of spinning: `src/core/timer_queue.cpp`).
3. **"Renderer CPU 7.9 ms" is wall time, and the ring thread is not the limiter.** `CpuTimer`
   (`native_renderer.cpp:6567`) is `steady_clock` around `Draw`, `Resolve` and `Swap`; `Swap`
   includes the blocking `vkAcquireNextImageKHR` and present (the "swap 1.87 ms (present 1.83)"
   line). The figure is 7.3-8.4 ms in every gameplay window of `fear_029` whether the frame has
   641 or 1,227 draws: it tracks the frame period, not the draw count. The ring thread's wall time
   in the heaviest window: 38% our code, ~16% Vulkan calls proper (each a Wine PE-to-Unix thunk),
   8% acquire, 3% present, 27% waiting (15% in `SleepUntil`'s timer inside `WAIT_REG_MEM`, 12% in
   the idle `ring_cv_`), and it reports **2.2 ms per frame waiting for the game**. In heavy scenes
   the two over budget are the game thread (10.8-12.4 ms) and the GPU (10.1-11.3 ms). The ring
   thread's work matters for power, and for the 1.5x goal later, not for 90 fps now. So W2 ranks
   below W1, W3 and W4.
4. **How to read the profiler's numbers.** A thread's "% of a core" is CPU time
   (`GetThreadTimes`). The breakdown after it is by *samples of the instruction pointer*, taken
   whether the thread was running or blocked, so "`ZwWaitForSingleObject` 15%" is time spent
   blocked, not CPU. A blocked thread showing CPU (XThread021C: 7.7% of a core, 94% in a wait) is
   the cost of Wine's waits themselves (client side; the wineserver's share is in another process
   and invisible here).
5. **The UI thread's half core is the SDL event loop being woken per controller poll.** Every
   `XamInputGetState`/`GetKeystroke`/`SetState` reaches `SDLInputDriver::QueueControllerUpdate`,
   which posts `SDL_PumpEvents` to the UI thread (`CallInUIThread`, one in flight at a time). Each
   wake is a `PeekMessage` sweep plus `MsgWaitForMultipleObjectsEx`, both wineserver round trips
   under Proton. With HIDAPI off, SDL's Windows gamepad backend is XInput, which Wine implements on
   HID: that is where `xinput1_4.dll` and the `wine_xinput_hid_update` thread (2.9%) come from;
   `input_backend` is `sdl` alone (the log: "SDL input driver initialized"). `RefreshDevices()` on
   every input export is separate and runs on the game thread.
6. **The generated code is compiled with `-fasync-exceptions` and with Tracy zones in every
   function.** `generated/rexglue.cmake` adds `/EHa` (clang: `-fasync-exceptions`) "for guest SEH
   scopes", but `generate_exception_handlers` is off for F.E.A.R. (no `SEH_TRY` in either game's
   output), so it buys nothing and taxes every function: the compiler must treat every load, store
   and call as a potential throw. `REXGLUE_ENABLE_PROFILING`/`TRACY_ENABLE` are defined for the
   recomp target too, so `REX_FUNC_PROLOGUE()` is a `ZoneNamedN(..., TracyIsStarted)` in all
   94,608 functions. Both are measurable, zero-risk flag changes (section 3, item 6).
7. **GPU passes:** 43,911 passes and 8,866 resolves over 739 frames in the heaviest window: **59
   passes and 12 resolves a frame.** `BeginPass` splits a pass on any change of the exact
   attachment set (`targets == pass`), and every attachment is `LOAD_OP_LOAD`/`STORE_OP_STORE`
   (`native_renderer.cpp:3163-3176`), as W3 suspected. Resolves also end the pass.
8. **The barrel dip is both sides at once:** 12.4 ms of game thread and 11.3 ms of GPU in the same
   window, with the ring thread idle 2.2 ms a frame. Fixing one side alone gives nothing visible.
9. Smaller: "engine sound +12%" is 0.06 ms per frame between two different play sessions, i.e.
   noise. The `rexruntimerd.dll` +0.10 ms is patch 0013's saves, `memcpy`/`memset` included
   (`HANDOFF-REGOPTS.md` 7.1). `[rexcrt]` strings can't give much: the whole C runtime is 0.21 ms
   a frame after stage B.

### 2. Ranked list
Gains are estimates from `fear_029`'s heaviest window (1,227 draws, 90 Hz target) unless marked
measured. "Core" = share of one Deck core; the APU's 15 W is shared with the GPU, so cores saved
are GPU clocks gained in heavy scenes (measure with MangoHud, section 4).

| # | Change | Where | Gain | Risk | Measure | Depends on | Size |
|---|---|---|---|---|---|---|---|
| 1 | Don't start `WatchDisplay` under Wine | `native_system.cpp` `SetupGuestGpu` | **0.5 core** | none (a report histogram goes blank) | thread 504 gone from the profile; MangoHud GPU clock in the barrel scene | - | quick |
| 2 | Timer queue: blocking wait | `--timer_queue_blocking_wait`, then default in `settings.toml` | 0.15 core | timer latency for guest `KeSetTimer`/`KeDelayExecution` (the vblank is its own thread) | profile line gone; sound and animation unchanged | test first (section 5) | quick |
| 3 | Drop `-fasync-exceptions` and Tracy from the recomp target | `port-nx/CMakeLists.txt` (override `REXGLUE_RECOMP_OPTIONS`, undefine `TRACY_ENABLE`) | 3-10% game thread (**unmeasured**) | none for F.E.A.R. (no SEH scopes generated; faults are handled by the vectored handler) | PC A/B, `abcompare.py` game-thread ms per 1,000 draws | - | quick |
| 4 | UI pump: at most one `SDL_PumpEvents` per 4 ms, and count the wakes | SDK `sdl_input_driver.cpp` `QueueControllerUpdate` | 0.2-0.4 core | input read up to 4 ms later than the poll that asked for it; the game reads the state of the previous pump today, so the feel is unchanged | thread 332's share; the new wakes/s counter | counter first | quick |
| 5 | Ring thread: wait on `ring_cv_` instead of a waitable timer in `WAIT_REG_MEM` | `native_system.cpp` PM4_WAIT_REG_MEM | 0.1-0.2 core here, plus wineserver time | none (same 200 µs bound) | `ZwSetTimer`/`ZwWaitForSingleObject` gone from the ring thread | - | quick |
| 6 | `memcpy`/`memset` hooks without the xmm clobber | SDK `function.h`, `hook.h` (`REXCRT_EXPORT`) | 0.1 ms game thread (measured: 0.23 to 0.33 ms) | none | `rexruntimerd.dll` module ms | - | quick |
| 7 | GPU: per-pass profile, then pass merging with `VK_EXT_dynamic_rendering_unused_attachments` | `BeginPass`, pipeline creation | 1-3 ms GPU (**guess**; the profile decides) | visuals if a merged pass keeps an attachment the game aliased; pipelines must declare unused attachments | `--nx_native_gpu_profile` before and after | item 7a (section 5) | medium |
| 8 | Stage C: `non_argument_as_local` | manifests | 3-8% game thread (**unmeasured**) | low: the scan's remaining entry-reads are funclets (patch 0007), `__chkstk` (harmless), unreferenced code and one scan false positive fixed here | PC A/B with the deterministic checks of #34 | 0007-0013 built and clean | medium |
| 9 | PGO for the recomp target | `tools/build-nx.ps1` (`-fprofile-instr-generate`, a PC play session, `-fprofile-instr-use`) | 10-20% game thread typical for branchy translated code (**unmeasured**) | none at runtime; build pipeline work | PC A/B | - | long |
| 10 | Direct3D spin to a wait (#32) | hook on `sub_82973C88`, event from the ring thread's swap/fence | power at capped rates (24% of the game thread at a 60 cap, 4% uncapped) | feel if the wake is late: keep a short timeout | profile "Direct3D wait" share, MangoHud CPU power | - | medium |
| 11 | Profiler: host code visible | `profiler.cpp` `Bucket`, a symbolize script | measurement | none | - | - | quick |
| 12 | ThinLTO for the recomp target | CMake | 5-10% game thread (**unmeasured**) | build time and memory | PC A/B | - | medium |
| 13 | Re-key saved pipelines by the current sample count | pipeline prewarm | hitches at MSAA off | none | compile waits at level start | - | quick |
| 14 | `ldmxcsr` count, then a decision | counter in the fpscr switch | unknown | precision (denormals) if FTZ is made permanent | counter per frame | counter first | small |
| 15 | Vertex hashing by write watch instead of per-draw hashing | `Draw` fetch path | ~0.8 ms ring-thread wall (off the critical path) | the Deck corruption fix (#27) | `vertex spans` MB hashed | - | long |
| 16 | Fewer Vulkan calls per draw (push constants, descriptor buffers) | shader translator and `Draw` | ~1-1.5 ms ring-thread wall (off the critical path) | large change | winevulkan share | 1.5x work | long |

Quick wins: 1-6, 11, 13. First batch for the Deck: 1, 2, 4, 5 (power, ~1 core) plus 3 and 6
(game thread), measured on the PC first.

### 3. Implementation details for the top items
**Item 1, `WatchDisplay`.** In `SetupGuestGpu`:
`if (!display_pacing_ && !fear::display_vblank::UnderWine()) display_watch_ = std::thread(...)`.
`display_watched_` stays false, so `RecordPacing` still runs (it only reads `display_vblanks_`,
which stays 0) and the pacing line's histogram shows everything in bucket 0; print "display
refreshes: not watched under Proton" instead when `!display_watched_`. Nothing else reads
`display_vblanks_` in timer mode. On native Windows the thread stays (its `WaitForVBlank` is a
real wait there). Test: the profile no longer lists a `dxgi.dll` thread; MangoHud's
`gpu_core_clock` in the barrel scene against `fear_029`.

**Item 2, timer queue.** No code: `--timer_queue_blocking_wait` in the launch options
(`--name` is on). `TimerQueue::TimerThreadMain` then waits in disruptorplus's
`blocking_wait_strategy` (a condition variable, `NtWaitForAlertByThreadId` under Wine) until the
next due time or a new item. Check in the log that `rex::thread::TimerQueue` leaves the profile
and that nothing timing-based changes (footsteps and weapon sounds, animation, the autosave).
Then `timer_queue_blocking_wait = true` in `make_package.py`'s `settings.toml`.

**Item 3, flags for the generated code.** In `port-nx/CMakeLists.txt`, after
`include(generated/rexglue.cmake)` and before `rexglue_setup_target(fear)`:
`set(REXGLUE_RECOMP_OPTIONS "")` (or keep only the `-gline-tables-only` entry), and for the
`fear_recomp` target `target_compile_definitions(fear_recomp PRIVATE TRACY_ENABLE=0)` is not
enough (the SDK's headers test `#ifdef`); instead give the recomp target its own
`REX_FUNC_PROLOGUE` by compiling it without `REXGLUE_ENABLE_PROFILING` and `TRACY_ENABLE`
(`rexglue_apply_recomp_settings` is where the SDK adds them: pass a flag, or strip the two from
`COMPILE_DEFINITIONS` on `fear_recomp` after setup). Correctness check before the build: `grep -c
SEH_TRY port-nx/generated/default/*.cpp` is 0 (it is today), and the write-watch handler is a
vectored exception handler, which needs no C++ EH mode. Measure on the PC: `abcompare.py` game
thread ms per 1,000 draws and `engine` module ms, same scene. Both games.

**Item 4, UI pump.** In `SDLInputDriver::QueueControllerUpdate`, keep the existing
`sdl_pumpevents_queued_` flag and add `std::atomic<int64_t> last_pump_ns_`: return early if a
pump ran less than 4 ms ago (`steady_clock`), else queue it and stamp the time inside the queued
lambda. Count `pumps_` and `polls_` (atomics) and print them per 10 s from the profiler's report
(or a `[hid]` line): polls/s, pumps/s. The first run with just the counters says how many wakes
the loop takes today (my reading of the 49.5% is several hundred a second); the 4 ms cap bounds
it at 250/s. The game's reads are unchanged in kind: `GetState` returns SDL's last pumped state
today too. If 4 ms still leaves thread 332 high, the remaining cost is SDL's `SDL_WaitEvent`
itself under Wine; then pump from a timer on the UI thread at 250 Hz and make `GetState` not
queue at all.

**Item 5, ring thread wait.** In `PM4_WAIT_REG_MEM`'s loop, replace
`rex::thread::SleepUntil(Clock::now() + 200us)` with a wait on `ring_cv_` under `ring_mutex_`
with `wait_for(lock, 200us)` and no predicate (a WPTR write wakes it early, which is also what
usually ends the wait: the game writes the ring after releasing what the ring waits for). Keep
the 16 yields. The condition variable is `NtWaitForAlertByThreadId` under Wine (a futex, no
server round trip); the waitable timer costs `NtSetTimer` plus `NtWaitForSingleObject`, both
server calls, 11-20 times a frame. Same bound, same `kWaitRegMemMax` limit. Measure: the two
`Zw*` entries leave the ring thread's list; `per frame: ... ms waiting` unchanged.

**Item 6, rexcrt without the clobber.** `HostToGuestFunction<Func>` gets a second template
parameter `bool keep_xmm = true`; `REX_KEEP_CALLEE_SAVED_XMM()` becomes
`if constexpr (keep_xmm) REX_KEEP_CALLEE_SAVED_XMM();`. Add `REX_HOOK_NO_XMM(name, function)`
that instantiates `<function, false>`, and make `REXCRT_EXPORT` use it (`hook.h:243`). The CRT
hooks (`src/kernel/crt/memory.cpp`) make no system call, so nothing inside them can change the
registers. Re-export as SDK patch 0014.

**Item 7, GPU passes.** First `--nx_native_gpu_profile` on the Deck in the barrel scene and the
benchmark room (section 5). The report names the top 14 keys by `pass c=<target> d=<target>` and
`resolve ... WxH` with ms per frame, count per frame and draws; what we need from it: ms in
passes vs resolves vs the swap blit, the number of passes with few draws (pass overhead), and
the resolves' share. If pass overhead is the story: query
`VK_EXT_dynamic_rendering_unused_attachments` in the Vulkan provider (RADV has it), create
pipelines with `VK_FORMAT_UNDEFINED` for the attachments a draw's shader doesn't write, and in
`BeginPass` keep the current pass when the new set is a *subset* of the active one (the superset
rule the trace at line 3111 mentions): the draw's color write masks already stop writes to the
others. Keep the exact rule when the extension is missing (NVIDIA on the PC has it too). What it
must not change: the EDRAM alias handling (`rt_generation`, `MirrorAliasClear`): a pass may only
continue if no attachment in it was invalidated since it began. Measure with the same profile
line, then the frame-time and screenshots of the three standard scenes.

**Item 8, stage C.** The remaining risk from `regflow.py`'s "nonarg" groups: 85 funclets (patch
0007 hands r12 over), `__chkstk` (reads r12 = 0, returns at once: harmless), `sub_82BBCA20` and
four unreferenced functions, and two Havok functions the scan flagged for `vperm128` reading its
destination, which it does not (a scan false positive, fixed in this pass). Plan: build stage C
on the PC with 0007-0013, run the #34 checks (menu items, tracers, AI, save/load, a level in each
game, physics props, a texture load), then `abcompare.py` against stage B in the same scene.
Expected: r0, r2, r11, r12 and f0 are MSVC's scratch registers, 1.5 M references in the
translation against 1.0 M for the non-volatiles stage B localized; the gain should be of stage
B's order (-5.7% game thread) or less, since many of them are already short-lived.

**Item 11, host code in the profiler.** `Bucket()` (`profiler.cpp:199`): for an `ip` inside
`fear.exe`, after `upper_bound` on `g_functions` check `ip < next->start` (the next entry's host
address) or, for the last entry, `ip - start < 64 KB`; otherwise the sample is host code. Bucket
it as `kHostTag | ((ip - g_exe_base) & ~0xF)` and name it `fear.exe+0x<rva>`; in `ModuleKey`
map these to a "fear.exe (host)" module. Then a script `tools/analysis/symbolize_profile.py
<log> <fear.exe>`: collect every `fear.exe+0x` RVA, run `.tools/llvm/bin/llvm-symbolizer
--obj=<fear.exe> --functions=short --inlining=false` once with all of them (it reads the `.pdb`
next to the exe; the Deck package's `fear.exe` is the PC build's, so the PC's `fear.pdb`
matches), and rewrite the lines. The 16-byte rounding keeps the bucket count bounded; the ring
thread's top functions (state hashing, `XXH3`, the fetch loop, `Allocate`, `Resolve`) will show
by name. The "modules" line then separates our renderer from the translated code at last.

### 4. Measurement to add first
- Item 11 (host symbols) and item 4's counters (polls/s, pumps/s), plus two more counters in the
  `[native]` report: WPTR writes per frame and ring-thread wakes per frame.
- Kernel exports by count: a `static std::atomic<uint32_t> calls` per `HostToGuestFunction`
  instantiation, registered with the function's name in a table at static init, top 10 per 10 s
  in the profiler report. Answers "which exports does the game call most" without symbols.
- **Power and clocks without a build: MangoHud.** Launch options:
  `MANGOHUD=1 MANGOHUD_CONFIG=output_folder=/home/deck/mangohud,log_duration=300,autostart_log=10,no_display,fps,frametime,gpu_core_clock,gpu_power,gpu_load,cpu_mhz,cpu_power,cpu_load WINEDLLOVERRIDES=... %command% ...`
  (the overlay off, a CSV per run). That answers the W1 question directly: GPU clock and power
  in the barrel scene before and after items 1, 2, 4, 5. Reading sysfs from the profiler
  (`Z:\sys\class\hwmon\hwmon*\name` == `amdgpu`, then `power1_average`, and
  `Z:\sys\class\drm\card*\device\pp_dpm_sclk`) works under Wine (plain file reads) and is safe
  (read-only), but MangoHud gives the same with no build; add the sysfs read only if the
  per-window correlation with our own report is wanted.
- `--nx_native_gpu_profile` is the W3 measurement; nothing to add until its report is read.
- An `ldmxcsr` counter (item 14) behind a define, PC only.

### 5. Deck tests without a build (one at a time, same three scenes, profiler on)
1. `--timer_queue_blocking_wait` (item 2).
2. `--nx_native_gpu_profile` (item 7a): read the `[native] GPU by pass` lines.
3. `--nx_native_record_thread`: the ring thread's wall time with the Vulkan calls on their own
   thread; a 5th busy thread on 4 cores, so watch the game thread's share and MangoHud's CPU
   power. Expected: no fps change now (the ring thread has slack), useful data for the 1.5x
   question.
4. MangoHud logging (section 4) on an unchanged build, for the baseline.
5. A short run with `--no-fear_vertex_exact --no-fear_vertex_spans` to measure the hashing cost
   in the ring thread's wall time (expect corruption: measurement only).

### 6. False leads
- **The ring thread as a frame-rate limiter today** (corrections 3 and 8): its waits show slack.
  Its work is a power item until the GPU and the game thread are under 10 ms.
- **Acquire and present on their own thread, push descriptors, descriptor buffers, bindless,
  multi-draw** (W2): each buys ring-thread wall time that is not on the critical path. Keep for
  the 1.5x work, after item 11 shows where the 38% actually goes.
- **More `[rexcrt]` functions** for speed: the whole C runtime is 0.21 ms a frame now. Map them
  for correctness reasons if any, not for performance. The heap group only if the allocator ever
  shows in the per-module line.
- **"Engine sound went up 12%":** 0.06 ms between different sessions. Nothing to chase.
- **The `dxgi.dll` thread as Wine's or the D3D12 backend's:** it is `WatchDisplay` (item 1).
- **1.5x at over 100 fps:** the GPU is 8.5 ms a frame at 1x (10-11 in heavy scenes) and 1.5x is
  2.25 times the pixels. Even if items 7 and the pass work halve the GPU cost, 1080p lands near
  60-70 fps in heavy scenes, not 100. A docked 1080p60 target is realistic after the GPU work;
  say so on #40 rather than carry the stretch goal as stated.
- **Cutting the texture upload burst at level start** (644 uploads in 186 ms): one hitch per
  level, already off the play path; not worth a change now.

### 7. Changed in this pass
`tools/analysis/regflow.py`: `vperm128` is no longer taken to read its destination (its fourth
operand is the permute control), which removes two false positives from stage C's risk list.
This section. Nothing built.

## Batch 1 (2026-10-23): items 1, 3, 4, 5, 6 and 11 coded, type-checked, not built
Every changed file type-checks with the build's own flags (both games); the CMake change was
checked in CMake's script mode. Nothing is built or run yet.

| Item | Change | Where |
|---|---|---|
| 1 | No `WatchDisplay` thread under Proton; the pacing line says "display refreshes not counted under Proton" instead of an empty histogram | `native_system.cpp` `SetupGuestGpu`, `Report` |
| 3 | `-fasync-exceptions` dropped from the generated code (sources and the PCH), both games; configure fails if a manifest ever turns `generate_exception_handlers` on | `port-nx/CMakeLists.txt`, `port-files/CMakeLists.txt` |
| 4 | SDL pumps at most every `hid_sdl_pump_interval_ms` (4; 0 = one per poll, as before); a `[hid] SDL: N controller polls/s, M event pumps/s` line every 10 s | SDK patch 0015, `sdl_input_driver.cpp` |
| 5 | `WAIT_REG_MEM`'s 200 µs sleep is `fear::display_vblank::ShortSleep`: under Proton ntdll's `NtDelayExecution` (not alertable), elsewhere the high-resolution timer as before | `display_vblank.{h,cpp}`, `native_system.cpp` |
| 6 | `REX_HOOK_LEAF` (`HostToGuestFunction<Func, false>`) for the ten hooks in `crt/memory.cpp`: no xmm6-15 saves around `memcpy`/`memset` | SDK patch 0014 |
| 11 | Host code in the exe goes under the function holding it, `fear.exe+0x<RVA>`, by the exe's unwind table; the translated code ends at the last translated function's end. "modules" shows it as "fear.exe (host code)". `tools/analysis/symbolize_profile.py LOG EXE` names them from the PDB (tested on the current build) | `profiler.{h,cpp}`, new script |

Item 2 needs no code: `--timer_queue_blocking_wait` in the launch options for the Deck run.

**Where this differs from Fable's details, and why:**
- **Item 3: there are no Tracy zones in the generated functions.** `REX_FUNC_PROLOGUE` adds a zone
  only with `REXGLUE_PROFILE_GUEST_FUNCTIONS`, which nothing defines (`build.ninja`: 0
  occurrences). So the change is the EH flag alone, and its gain is unknown until the A/B.
- **Item 5: not a condition variable.** MSVC's `condition_variable::wait_for` rounds the timeout
  up to whole milliseconds, so a 200 µs wait would become 1 ms whenever no WPTR write wakes it, and
  a wait the game releases with a plain memory store gets no WPTR write. Under Wine,
  `NtDelayExecution` without alerts sleeps inside the process (`select`/`nanosleep` for the time
  left, no wineserver call) at 100 ns resolution, so the bound stays 200 µs. On Windows it would
  round to the system timer, so the waitable timer stays there. Check in the Deck profile: the ring
  thread's `ZwSetTimer`/`ZwWaitForSingleObject` give way to `ZwDelayExecution`, and its
  `per frame: ... ms waiting` doesn't grow.
- **Item 6: the hooks are `REX_HOOK`s in `crt/memory.cpp`**, not `REXCRT_EXPORT` (which nothing
  uses), so they switch to the new `REX_HOOK_LEAF`. Safe for the reason Fable gives: none of them
  reaches the kernel, and a page fault in them (the write watch) returns through a full context
  restore, like the translated code's own stores.
- **Item 11: the unwind table, not a 64 KB guess.** `RtlLookupFunctionEntry` gives each host
  function's start (Wine implements it), and the last translated function's end bounds the
  translated code. Leaf functions without an entry fall back to their 16-byte line.

**SDK:** patches 0014 and 0015 are commits `e322091` and `11c330c` on the SDK's branch
`register-options`, after 0013's `490d264`.

**Built 2026-10-23** (`fear.exe` 19:51, `fearfiles.exe` 19:52; A side saved in
`private/ab/A-stageB-5a2fdd4` and `-files`). Checked in the binaries: no `-fasync-exceptions` in
either game's `build.ninja`; the `memcpy` hook's wrapper saves rsi, rdi and rbx only, and the
runtime DLL's xmm15 saves went from 640 to 630 (the ten leaf hooks).

**PC A/B (2026-10-23, Interval 2 story play, 3x, MSAA honoured, texture pack, 120 cap, profiler
on).** A `run-20261023-195448-A-stageB-5a2fdd4.log`, B `run-20261023-200444.log`. The user: both
the smoothest PC runs of the day, B a little better; no visual glitches (one decal flashed once in
A). `abcompare.py`: game thread 1.65 / 1.69 ms, renderer CPU 1.00 / 1.00, GPU 2.95 / 3.00 ms per
frame: the same within noise, as expected on a PC this fast at a 120 cap (the Deck is where batch
1 should show). What B shows:
- **The game polls the controller 8 times a frame**: `[hid] SDL: 960 controller polls/s, 120 event
  pumps/s`. Before patch 0015 each poll could wake the UI thread; now it's one pump a frame.
- **Item 11 didn't work in this build**: the ring thread's host code still showed as
  `sub_8307D8A8`. The function table (`PPCFuncMappings`) also lists the import thunks
  (`__imp__XexUnloadImage`, `rexcrt_memcpy`, ...), which sit at the very end of `.text`, after the
  renderer, so "the end of the last translated function" bounded nothing. **Fixed after the run,
  not built yet:** a sample is the translated function's only if the unwind table's entry for it
  starts at that function (or, for a leaf function with no entry, within 64 KB of a start that
  has none). Checked against this build's `.pdata` and PDB: all 94,371 translated functions
  classify as translated (46,708 of them leaves), all 6,070 host functions after them as host.
- **A GPU spike in Docks' closing transformation sequence** (in A too, so not new): #41.

**How to test (ask the user first).**
1. Keep the current `fear.exe` and `fearfiles.exe` (stage B with 0013) as the A side
   (`private/ab/`), then build both games: a full recompile of the translated code, since its
   flags changed.
2. PC A/B, profiler on, same scene: `abcompare.py` for game-thread ms per frame and per module;
   the `[hid]` line; `symbolize_profile.py` on the B log for the ring thread's functions.
3. Deck: the programs update zip; launch options as before plus `--timer_queue_blocking_wait` and
   MangoHud's logging (section 4). Same three scenes. Against `fear_029`: thread 504 gone,
   thread 332 and TimerQueue down, the ring thread's waits, MangoHud's GPU clock and power in the
   barrel scene, and nothing different in feel or sound.
