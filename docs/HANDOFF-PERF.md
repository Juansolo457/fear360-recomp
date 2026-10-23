# Handoff: Steam Deck performance (reviewed; next is the build and a Deck run)

Written 2026-10-22 for a second opinion on the Deck performance work, **reviewed the same evening
(Fable, adversarial review; its findings are on #28)**, and updated for whoever continues. The user's
standing rules: review the code and the plan, change what is wrong, and **don't build or launch the
game without asking the user**. Tracking issue: GitHub
[#28](https://github.com/Juansolo457/fear360-recomp/issues/28) (all the numbers below are in its
comments, too). General project state: `docs/HANDOFF.md`, `docs/PROGRESS.md`, `docs/REVIEW.md`.

## Where it stands (read this first)
- **2026-10-23: a second review is asked for: `docs/HANDOFF-REVIEW.md`** (the Deck corruption
  fix, the profiler's modules/waits/system-call callers, native `memcpy`/`memset`). The sections
  below are the history, newest first.
- The Deck's corrupted debris and hands are fixed by `fear_vertex_fresh` (on by default since
  `54135f6`); the best Deck run so far is `fear_015`. Built and zipped, not yet run: `c02cea4`
  (system-call callers, native `memcpy`/`memset`).

## Overnight analysis (2026-10-23): `docs/ARCHITECTURE.md`
Static analysis only, nothing built or run. The main-thread `ZwCreateEvent`/`ZwDeviceIoControlFile`/
`ZwClose` trio matches the Deck controller's rumble: SDL's Steam Deck driver sends each change as a
synchronous HID feature report on the game thread, and Wine's `HidD_SetFeature` creates and closes
an event per report. **Test first, no build: vibration off in the game's options.** The hottest
main-thread function, `sub_82973C88`, is the 360 Direct3D's spin loop waiting for the GPU (fence,
ring space, swap throttle): 24% of the thread at 60 fps (idle time burnt as power), about 1% in the
slow fights. Those are CPU-bound: 19 ms of game-thread work per frame (median), with the renderer
thread at 4.6 ms and the GPU at 13 ms. The doc ranks the options (rumble thread, per-module profile,
`[rexcrt]`, the translator's register options, a native Direct3D layer) and maps the executable.
Filed as #29 (rumble), #30 (profiler), #31 (`[rexcrt]`), #32 (Direct3D spin), #33 (SDK
spinning), #34 (register options) and #35 (native Direct3D research).

## Profiler: modules and Direct3D wait reasons (#30, built 2026-10-23, not run yet)
With `--fear_profile true`, each thread's line in the report gets two more:
`[profile]       modules: ...` (its samples by part of the game, from the module table in
`port-nx/src/profiler.cpp`, one per game, and by DLL outside the translated code) and, for a thread
that waited in Direct3D, `[profile]       Direct3D waits: ...` (by the XDK's reason: primary or
secondary ring full, swap throttle, fence, resource release). Two manifest hooks feed the waits:
`FearD3DWaitBegin` (r5) and `FearD3DWaitEnd`, at `0x82973A50`/`0x82973A80` (F.E.A.R.) and
`0x82973648`/`0x82973678` (Files). Both ports built cleanly; nothing has run them yet. Still open on
#30: naming the translated function that made each system call.

## Next build (2026-10-23): an SDK switch for SDL's HIDAPI drivers, the full caller chain
- SDK patch `patches/rexglue-sdk-nx/0004-...`: cvar `hid_sdl_hidapi` (auto/on/off) sets
  `SDL_HINT_JOYSTICK_HIDAPI` with override priority before the gamepad subsystem starts, in
  `SDLInputDriver::OnWindowAvailable` (on the UI thread). The log line
  `SDL: HIDAPI joystick drivers on|off (hid_sdl_hidapi ..., SDL_JOYSTICK_HIDAPI in the
  environment: ...)` also says whether Steam passed the variable last time.
- Profiler: `system calls from:` lists up to four other modules on the way out (each once,
  innermost first; the exe's host code as `fear.exe`), then the first translated function.
- Deck test: `--hid_sdl_hidapi off` at the end of the launch options.

## Deck run with `SDL_JOYSTICK_HIDAPI=0` in the launch options (`fear_017.log`, same build, Docks)
- **It didn't take effect**: SDL still added "Steam Controller" (28DE:1302) and "Steam Deck
  Controller" (28DE:1205), and `ZwDeviceIoControlFile < HID.DLL` is still 6.6-8.7% of the game
  thread (+1-2% each for `ZwCreateEvent`/`ZwClose`). Either Steam didn't pass the variable or
  SDL finds the pads another way that also uses `hid.dll`; the log can't tell. The SDK sets no
  SDL joystick hints itself (only audio category and video driver).
- Performance about the same, as the user felt: 67% of windows at 59.5+ (fear_016: 74%), heavy
  windows median 56.0 (57.9), slowest 45.8. Run-to-run variation, nothing changed.
- The autosave input drop came back (#36 reopened): intermittent, present in fear_015 and
  fear_017, not fear_016.
- The translated function at the end of the HID chain is mostly `sub_822263B8`, a recursive tree
  walk with no imports (calls only itself and `sub_822261D0`): it can't be the real caller, so
  that end of the scan is picking up stale frames. To pin the path: record every non-system
  module on the chain (is it `rexruntimerd.dll` with SDL inside, `dinput8.dll`, `xinput1_4.dll`?),
  and set the SDL hint from code with a switch instead of the environment.

## Deck run of `40af21a` (`fear_016.log`, 2026-10-23, Docks into the water treatment facility)
- User: the last firefight was the best yet, close to 60 and smooth with no visual hiccups; an
  earlier fight still dropped into the 40s (about midway, a tight room full of debris and
  particles with three or four enemies: the benchmark scene). The autosave input drop is gone
  (#36 closed).
- 56 windows, mostly 60 fps. Slowest: 08:09:10 at 52.2 (1,470 draws a frame; 77 of 522 frames
  took two or more refreshes, longest 41.8 ms) and the last window, 08:14:10, at 42.4 (132 of 423
  frames late, longest 84.5 ms; engine sound 15% of the game thread there, as in fear_015's 44 fps
  window).
- **The syscall trio is Wine's `hid.dll`** (`system calls from:`): `ZwDeviceIoControlFile <
  HID.DLL` 7.6-10.5%, `ZwCreateEvent` and `ZwClose < HID.DLL` 1-2% each: 10-13% of the game thread
  in play, about 2.5 ms of a heavy frame. The translated function at the end of the chain varies
  (stale stack words). Rumble is ruled out (#29, retitled). Next, no build: launch with
  `SDL_JOYSTICK_HIDAPI=0` in front of the options; SDL then takes the controls from Steam Input's
  virtual pad instead of its HIDAPI drivers.
- Native `memcpy`/`memset`: the C runtime went from 7% to 6% of the game thread in heavy windows
  (median of 16 each): about one point.

## Next build (2026-10-23): system-call callers (#30) and native memcpy/memset (#31)
- Profiler: samples in ntdll, win32u, kernelbase or kernel32 get a `system calls from:` line,
  naming the first other DLL and the first translated function among the return addresses on
  the stack (16 KB of it copied with `ReadProcessMemory` while the thread is suspended, scanned
  after; a word counts when it points just past a call instruction). Answers what makes the
  constant `ZwDeviceIoControlFile`/`ZwCreateEvent`/`ZwClose` trio (11-14% of the game thread).
- `[entrypoint.rexcrt]` in both manifests: `memcpy`/`memset` at `0x8287D240`/`0x8287B540`
  (F.E.A.R.) and `0x828728A0`/`0x82871980` (Files, a newer XDK's memcpy with a 4-byte fast
  path), replaced by the SDK's host versions. Expect a few percent of the game thread back.
- Autosave input drop filed as #36.

## Deck story run with exact vertex checks by default (`fear_015.log`, build `54135f6`, 2026-10-23, Docks)
- User: "the best run so far of the story content. It loaded the smoothest"; tracers fixed,
  frame rate "much more stable"; one decal looked briefly corrupted near the end ("like it had
  just a little millisecond loading in"), with nothing in the log to explain it (no stale-data
  catches, no skipped draws, no texture drawn before it was ready). Fresh was on (no
  `vertex reads:` line: that line only printed on a change, because the renderer's frame count
  starts at 1; fixed to print at the first swap, not built yet). Exact reads 600-1,570 MB per
  10 s, as in run 1.
- 52 gameplay windows: median 59.9 fps, 73% at 59.5+ (fear_007: 65%, fear_008: 69%). Over 1,000
  draws a frame: median 56.9, GPU 12.7 ms median, 13.5 max, renderer 6.9 ms. Slow frames are in
  menus and loading (the level's first frame: 192 ms creating 638 textures); in ~8 minutes of
  play only two over 50 ms (55, 53).
- Busiest windows (54-56 fps, 1,050-1,415 draws), game thread: engine 23-25%, ntdll 11-14% (the
  syscall trio), engine renderer 10-11%, Havok 8-10%, C runtime 7-8%, game client 4-7%, engine
  sound 4-6%, ClientFX 3-5%, Direct3D 4-5%, rexruntimerd 3%, game server 2-3%, AI planner 2%;
  swap-throttle wait 2-18%. The one 44 fps window had 18% swap-throttle wait and engine sound
  at 16%: a stall, not a busy fight.
- `Texture not handled: format 6, dimension 2, 16x16x16`: a 16³ RGBA8 volume (3D) texture,
  which the renderer doesn't support; for ~30 s (06:51:33-06:52:03) 2,092 lookups drew without
  it. The PC logs show the same warning. An effect that has probably always been drawn without
  its texture here; worth its own issue.

## Deck diagnosis runs (build `01fc8c4`, 2026-10-23, Instant Action "Interval 2")
- **Run 1, `fear_009.log`, `--fear_vertex_fresh true`: clean** ("looked clean, no stretched
  debris"). So the corruption is stale vertex or index data that one of the shortcuts let through
  (the once-a-frame check, the sampled/trusted ChangeDetector, the "changed lately" rule, or the
  span reads' cached index bounds). It was nearly free: 60 fps throughout (one 58 fps window at
  1,420 draws a frame), renderer CPU 3.3-8.4 ms a frame as in fear_008, ~100 MB/s hashed.
- Likely mechanism to check: the span reads (#27) copy only the vertices a draw's indices reach,
  with the bounds cached per converted index copy (`index_bounds`, keyed by the index range's
  hash). With the shortcuts that hash can be stale, the bounds then too narrow, and vertices
  outside the copied span are read from whatever follows it in the stream: a few corners far
  off. Fresh hashes the indices exactly, so the bounds are right. Run 2 (`--fear_vertex_spans
  false`, fresh off) says whether the spans are involved.
- **Run 2, `fear_010.log` (06:23), meant to be spans off: it ran the old behaviour.** The span
  reads were active and exact reads were at the old volume (200-270 MB per 10 s), so the spans
  flag didn't take: either an older build or the flag missing from the launch options (the
  missing `vertex reads:` line doesn't tell which: it only printed on a change). The
  corruption came back ("corruption was back"), and its video (`Desktop 2026.10.23 -
  06.25.16.09.mp4`) has one-frame glitch events in 0.43% of frames, like fear_008's 0.41%
  (PC clean run: 0.16%). Consistent with run 1, but says nothing about the spans. The spans test is still to do (`--fear_vertex_fresh false
  --fear_vertex_spans false` on a current build).
- **Run 3 (06:37, `--nx_native_msaa false`, the same old build): corruption back** (video
  `Desktop 2026.10.23 - 06.37.41.11.mp4`, 47 s: one-frame events in 0.75% of frames). MSAA isn't
  involved.
- **`fear_vertex_fresh` is now on by default** (both games, PC and Deck). Runs 2 and 3 on a
  build after this need `--fear_vertex_fresh false` to test the old behaviour. Still to check: the
  PC's renderer CPU at 120 fps and 3x with it on.
- **The new profiler, first output**: every Direct3D wait on the game thread was the swap
  throttle (reason 3), the game ahead of the display: 22-46% of the thread at 60 fps, 9% in the
  busiest window. No ring-full or fence waits. Busiest window (58 fps, 1,420 draws): engine 25%,
  ntdll 13% (the syscall trio), engine renderer 13%, Havok 10%, swap throttle 9%, C runtime 7%,
  Direct3D 5%, engine sound 5%, game client 4%, ClientFX 3%, game server 2%; AI barely shows on
  this map.

## Deck result of fear_008 (same 7d3a36c build, 2026-10-23, Instant Action "Interval 2", docked to a capture card)
- **Performance**, comparable with fear_007 and a little better: gameplay windows median 59.9 fps,
  69% of them at 59.5+ (fear_007: 65%), slowest 49.2 (45.6). Windows over 1,000 draws a frame:
  median 58.6 fps, GPU 11.9 ms median, 16.1 max, renderer thread 6.5 ms. The game still rendered
  1280x720 into a 1920x1080 swapchain (the dock's output), like before.
- **The `ZwDeviceIoControlFile`/`ZwCreateEvent`/`ZwClose` trio is constant**: about 11% of the game
  thread in quiet scenes and menus too, not only in fights. Rumble alone can't do that: the engine
  (`sub_821CFCD8`) reads all four pads every frame but sends vibration only when it changes. So
  the overnight rumble theory is at most part of it; the profiler needs to name the caller (#30's
  open item) before #29 is worth fixing.
- **The corruption is on video** (`Desktop 2026.10.23 - 05.19.46.07.mp4`, user's PC; frames 300 and
  306 saved in `private/captures/deck-008-impact-debris-frames300-306.jpg`): about 5 s in, the
  debris and dust from bullet impacts on a wall become huge polygons stretched toward the camera
  for a few frames; the tracer just before is fine. Particle geometry with some vertices far off.
  Ruled out from the log: skipped draws (none in gameplay), the pipeline wait budget (0 skipped),
  non-coherent stream memory (the renderer warns if so; it didn't), and the ring reporting its
  read position before executing a command buffer (it writes it back after). The PC's logs show
  the same stale-vertex counters, so they don't explain a Deck-only glitch either.
- **PC at 60 fps is clean** (2026-10-23, same build, Interval 2, `--fear_frame_rate_limit 60`,
  3x scale; recording `Videos\NVIDIA\F.E.A.R\F.E.A.R. 2026.10.23 - 05.43.58.08.mp4`): the user
  saw no corruption, it held 60 fps up to 1,767 draws a frame, and a scan of every frame for
  one-frame events (`transients.py`, scratchpad) found only muzzle flashes, sparks and blood
  (0.16% of frames, against 0.41% on the Deck, where the top events include the impact debris at
  0:05, the player's hands and weapon smeared over the screen at 1:40, and another at 4:09). So
  it's the Deck, not the frame rate. Smeared skinned models (hands) and particles both look like
  vertices with a far-off or near-zero w.
- GPU-dependent paths checked and fine by reading: float constants (all 256 uploaded when a
  shader indexes them dynamically), UBO offsets (aligned to the device's limit, 16 on RADV, 64 on
  NVIDIA), vertex copies and span fetch addresses (16-byte aligned, same code on both),
  one shared-memory binding on both (`maxStorageBufferRange` 4 GB).
- **Next on the Deck**: `--nx_native_msaa false` (no build), then a diagnostic build with
  switches that copy every draw's vertex data fresh (no reuse, no spans): if the corruption
  survives that, it isn't stale vertex data but the GPU side (RADV/ACO compiling a translated
  shader, or a robustness difference).

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
