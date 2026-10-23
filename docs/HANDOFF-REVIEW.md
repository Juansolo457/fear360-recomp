# Handoff: review of the 2026-10-23 work (Deck corruption fix, profiler, native C runtime)

Written 2026-10-23 for an independent review of everything done since `04efb2b` (the last commit
the previous review saw). Read the code, check the claims against it and the logs, and change
what's wrong. The user's rules: **don't build or launch the game without asking**, never close
the user's running game, never commit game data (see `CLAUDE.md`). Background:
`docs/ARCHITECTURE.md` (how the 360 executable is laid out and where the Deck's frame time goes),
`docs/HANDOFF-PERF.md` (the Deck runs one by one), tracking issue #28.

## Where it stands
| Commit | What | State |
|---|---|---|
| `8766059` | Profiler: samples by module, Direct3D waits by reason (#30) | built, run on the Deck (`fear_009`, `fear_015`) |
| `01fc8c4` | Renderer: `fear_vertex_fresh` and `fear_vertex_spans` switches | built, run (`fear_009`: fresh on, clean) |
| `54135f6` | `fear_vertex_fresh` on by default | built, run (`fear_015`: story level, the best Deck run) |
| `86a299a` | The `vertex reads:` log line prints at the first swap | built in `c02cea4` |
| `c02cea4` | Profiler: who makes system calls (#30); native `memcpy`/`memset` (#31) | built, **not run** (Deck zip of 07:12) |
| this commit | The stack scan reads only code sections, refreshed every second | type-checked, **not built** |

Deck logs are in `private/deck-logs/` (git-ignored): `fear_006`/`fear_007` (before today),
`fear_008` (Instant Action, corruption on video), `fear_009` (fresh on: clean), `fear_010` (old
checks: corruption back), `fear_015` (story, fresh by default). Videos are on the user's PC.

## 1. The Deck's corrupted debris and hands: `fear_vertex_fresh` (`native_renderer.cpp`)
**Symptom.** On the Steam Deck only, bullet-impact debris, tracers and the player's hands were
drawn for a frame or a few as huge polygons stretched toward the camera (video frames in
`private/captures/deck-008-impact-debris-frames300-306.jpg`). The PC was clean at 120 fps and at
60 fps, so not the frame rate. MSAA off didn't help.

**Fix.** `fear_vertex_fresh` (default on): every draw hashes its vertex and index data as guest
memory holds it now (`DrawRangeHash` always takes the exact path), and the span path (#27) is
taken whatever the range's history. A copy is reused only for byte-identical content. With it the
Deck was clean (`fear_009`, `fear_015`) at the same renderer CPU time (3-8 ms a frame).

**Likely mechanism, not proven.** The span reads copy only the vertices a draw's indices reach,
with the bounds cached per index buffer content (`index_bounds`, keyed by `RangeSlot(index_key)`,
a 64-bit mix of address, size and hash). Under the old rules that hash could be a sampled or
once-a-frame one, so after the game rewrote an index buffer the cached bounds could be an old
buffer's: too narrow, and the draw read vertices past its copy (whatever followed in the stream).
Why only the Deck: its renderer thread trails the game by a different amount, so the game's
writes land at other points relative to the once-a-frame checks. A clean test of this would be
fresh off, spans off (`--fear_vertex_fresh false --fear_vertex_spans false`); `fear_010` meant
to be that but ran without the spans flag.

**Please check:**
- Cost on the PC at 120 fps and 3x: not measured. A range whose vertex shader isn't span-safe is
  now hashed in full by every draw that uses it (`XXH3_64bits` over up to 16 MB). On the Deck the
  renderer's CPU time didn't move; the PC draws the same, but twice as many frames.
- Whether the old per-frame shortcuts are now dead weight (`RangeHash`'s unique-hash rule,
  `kVertexRecentFrames`, `kVertexAlwaysBytes` still run but no longer decide anything when fresh
  is on), and whether the 10-second report's "would have drawn stale" counters still mean what
  they say.
- `convert_fan`/`convert_loop` index paths have no key and are rebuilt every draw: fine?

## 2. Profiler (`port-nx/src/profiler.cpp`, `profiler.h`; issue #30)
With `--fear_profile true`, each busy thread's report line gets:
- **`modules:`** its samples by part of the game. `kFearModules`/`kFilesModules` are start
  addresses from `docs/ARCHITECTURE.md`'s map (RTTI, strings and call patterns via
  `tools/analysis/`; 64 KB granularity except the Direct3D starts). Files is laid out differently
  (its engine renderer sits inside the game client). Check the boundaries if a number looks odd.
- **`Direct3D waits:`** samples taken while the game's Direct3D waits for the GPU, by reason.
  Two manifest hooks at the first instruction of Direct3D's own wait bookkeeping
  (`FearD3DWaitBegin` with r5 at `0x82973A50`/`0x82973648`, `FearD3DWaitEnd` at
  `0x82973A80`/`0x82973678`) set a per-thread slot (64 atomics, no locks) that the sampler reads
  while the thread is suspended. Reason names follow the XDK's `D3DBLOCKTYPE` order; that is
  **inferred** from what each call site waits for (1 ring space, 2 secondary ring, 3 swap
  throttle from the present path, 6 fence, odd 7-15 resource release). On the Deck every wait was
  reason 3, the game ahead of the display.
- **`system calls from:`** (built, not run) for samples in ntdll, win32u, kernelbase or
  kernel32: the first other DLL and the first translated function among the return addresses on
  the stack. 16 KB of the stack is copied with `ReadProcessMemory` while the thread is suspended,
  then scanned; a word counts when it lies in a module's executable section and the bytes before
  it are a call (`FollowsCall`). Purpose: name the constant `ZwDeviceIoControlFile`/
  `ZwCreateEvent`/`ZwClose` trio (11-14% of the game thread). Rumble was ruled out: the engine
  (`sub_821CFCD8`) reads all four pads every frame but sends vibration only when it changes.

**Please check:**
- Lock safety while a thread is suspended: only `GetThreadContext`, an atomic load and
  `ReadProcessMemory` (a system call) happen before `ResumeThread`. Under Wine, `GetThreadContext`
  returns once the target is stopped in its suspend handler, outside Wine's own critical
  sections; is that right for Proton's Wine?
- The scan reads code bytes of modules listed up to a second earlier (`RefreshModules` once a
  second): a DLL unloaded within that second could fault the profiler. Acceptable for an
  opt-in diagnostic? A cheaper guard?
- Stale return addresses can name a wrong caller: say so when reading results.
- `TebOf`'s `ThreadBasicInformation` layout and `StackBase` at TEB+8 (x64 `NT_TIB`).

## 3. Native C runtime (`[entrypoint.rexcrt]` in both manifests; issue #31)
`memcpy`/`memset` are replaced by the SDK's host versions
(`third_party/rexglue-sdk-nx/src/kernel/crt/memory.cpp`, via `REX_HOOK`; pointer arguments and
the returned pointer are translated by `HostToGuestFunction`). F.E.A.R.: `0x8287D240`/
`0x8287B540`; Files: `0x828728A0`/`0x82871980` (a newer XDK; its memcpy adds a 4-byte fast path).
Identified by disassembly (`tools/analysis/x360img.py dis`), 2,021 call sites in F.E.A.R. and
3,442 in Files now call `rexcrt_memcpy`/`rexcrt_memset`; the guest bodies are gone from the
translation. `memcpy` alone was 1-4% of the game thread on the Deck. Not mapped: the SDK's
`wcscmp` compares the 360's big-endian UTF-16 in host byte order, which changes ordering.

**Please check:** that both pairs really are plain `memcpy`/`memset` over their whole bodies (not
`memcpy_s`, not a variant with extra side effects); that no caller depends on overlap behaviour
(the guest copies forward; the host's `memcpy` is assumed memmove-safe on x64 UCRT, check); and
the `[rexcrt]` all-or-nothing rule (only the heap group is enforced by the SDK).

## 4. Smaller items
- `tools/analysis/*.py`: the scripts behind `docs/ARCHITECTURE.md` (Python, run with `-I`;
  `x360img.py` needs `capstone` and `-P`).
- Issues filed today: #29-#35 (performance plan), #36 (controller input drops for about a second
  at every story autosave on the Deck; not investigated yet).
- Not filed yet: a 16x16x16 RGBA8 3D texture the renderer doesn't support
  (`Texture not handled: format 6, dimension 2`), seen on PC and Deck: some effect is drawn
  without it.
- An unexplained brief decal glitch near the end of `fear_015` (nothing in the log).

## 5. Can this reach the PC version's performance? (the user's question; a second opinion welcome)
- **On the user's PC it already does:** 120 fps (the cap) at 3x resolution, the same feel as
  the PC version with EchoPatch.
- **On the Deck**, 60 fps holds in most play (73% of `fear_015`'s windows at 59.5+), and the
  biggest fights dip to 54-57. The limit there is the game's own thread: about 17-20 ms of work
  per frame at worst, so holding 60 means cutting roughly 15-25% of it. Known levers, in order of
  likely size: the system-call trio (11-14%, if removable), the translator's register options
  (#34, the largest and least measured), native `memcpy`/`memset` (a few percent), then the
  Direct3D and engine work. On paper that's enough for a locked 60; it isn't proven until run.
- **Matching the PC version's raw speed on the same hardware isn't realistic without a
  source-level port:** the translated code carries the 360's register bookkeeping, byte swaps and
  calling convention everywhere, and the PC version is native x86 code. The gap can be narrowed
  a lot, not closed. The targets (120+ on PC, 60 on Deck) don't need it closed.
- It depends on the host's single-core speed: the game's logic runs on one thread. A faster
  handheld CPU gives more headroom than a faster GPU at 720p.

## How to build and package (ask the user first)
```
pwsh -NoProfile -File tools/build-nx.ps1                       # F.E.A.R. (port-nx)
pwsh -NoProfile -File tools/build-nx.ps1 -Project port-files   # F.E.A.R. Files
python -I tools/steamdeck/make_package.py --programs           # private/steamdeck/FEAR360-SteamDeck-update.zip
```
A manifest change regenerates the translation (about 2 minutes) and recompiles whatever it
touched; the `[rexcrt]` change recompiled most of it.
