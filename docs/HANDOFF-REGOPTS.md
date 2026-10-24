# Handoff: the translator's register options (#34), for review before any build

Written 2026-10-23. The user wants this reviewed **before anything is built or tested**. Nothing on
this branch has been compiled. The one thing that was run is the existing translator (master's
SDK, without the patches below) on the branch's manifests, writing into a scratch folder outside
the repo, to see what it emits (section 3). The user's rules: **don't build or launch the game
without asking**, never close the user's running game, never commit game data (see `CLAUDE.md`).
Background: `docs/ARCHITECTURE.md` ("The translated code" and "Tier 2", where these options were
first ranked), issue #34.

## What's proposed (branch `register-options`)
| Where | What |
|---|---|
| `port-nx/fear_manifest.toml`, `port-files/fear_manifest.toml` | Seven options on (`skip_lr`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `cr_as_local`, `non_argument_as_local`, `non_volatile_as_local`), `skip_msr` left off; `setjmp_address`/`longjmp_address`; `share_registers` on the C runtime's `_CallSettingFrame`, on Havok's v92 chain and (Files) on six split-off `__finally` blocks (section 6); the two menu hooks take more registers |
| `port-nx/src/fear_menu.cpp`, `fear_menu.h` | The menu hooks take r1 and r23-r29 as arguments instead of reading them from the thread's context |
| `patches/rexglue-sdk-nx/0007-0012` | Six translator fixes (0011 and 0012 from the review, section 6). They are also commits on the SDK branch `register-options` in `third_party/rexglue-sdk-nx` (`3dcdcd3`, then `047c9f7`, `e42a98c`); the SDK checkout itself is back at master's `b29317c`. Each one changes the output only when its option is on, so master's translation is unchanged |
| `tools/analysis/regflow.py` | The scan in section 3 |

Addresses (both games' executables, from disassembly):

| | F.E.A.R. | F.E.A.R. Files |
|---|---|---|
| `setjmp` | `82BBA450` | `82B95310` |
| `longjmp` | `82BBA200` | `82B94EC0` |
| `_CallSettingFrame` | `82BC0B70` | `828A0000` |

## 1. What the options do
Today every PowerPC register is a field of the shared `PPCContext` (`ctx.r31`, `ctx.cr6`, ...), so
each guest instruction is a load and a store through a pointer the C++ compiler has to assume
aliases guest memory. Each option makes one group of registers a C++ local in every generated
function, which the compiler can keep in host registers. Counts are of `ctx.` references in
today's generated code (F.E.A.R. / Files):

| Option | Becomes a local | References today | Notes |
|---|---|---:|---|
| `skip_lr` | LR: no `ctx.lr = X` before each `bl`, `mflr`/`mtlr` dropped | 274,672 / 292,002 | `blr` is already a plain `return;`. Breaks `blrl` (one per game): patch 0008 |
| `ctr_as_local` | CTR | 93,638 / 100,424 | |
| `xer_as_local` | XER (the carry) | 258,479 / 279,730 | |
| `reserved_as_local` | the `lwarx` reservation | 291 / 498 | |
| `cr_as_local` | CR0-CR7 | 480,513 / 523,892 | |
| `non_argument_as_local` | r0, r2, r11, r12, f0, v32-v63 | 1,511,868 / 1,528,763 | r12 carries a funclet's frame: patch 0007 |
| `non_volatile_as_local` | r14-r31, f14-f31, v14-v31, v64-v127 | 1,000,262 / 1,081,718 | the 39,300 / 42,502 calls to the save/restore helpers (`__savegprlr_N` ...) are dropped |
| `skip_msr` | **stays off** | | it drops `REX_ENTER_GLOBAL_LOCK`/`REX_LEAVE_GLOBAL_LOCK`, which the 202 `mtmsrd` sites (89 `mtmsrd r13,1`) use as the guest's critical sections |

Argument registers (r3-r10, f1-f13, v1-v13), r1 and r13 stay in the context: that's how values
cross a call. Unleashed Recompiled ships with all eight on. The gain for F.E.A.R. is unmeasured.

**`non_argument_as_local` needs `non_volatile_as_local`.** The save/restore helpers take their base
in r11/r12 (`addi r12,r1,-152; bl __savefpr_N`). They are dropped only under
`non_volatile_as_local`; with the non-argument registers local and the helpers still called, they
would store through a zero base. Never turn on the first without the second.

## 2. What the options break, and the fixes here
### 2.1 The menu hooks (`fear_menu.cpp`)
`FearMenuAddItems` and `FearMenuShowItems` found the thread's context by checking that the
register they were handed (r30, r31) was the context's own field, then read Options' layout
arguments from `ctx.r23`-`ctx.r29`. Under `non_volatile_as_local` those are locals: the check fails,
the hook logs "hook registers are not the thread's context", and PC Settings, Switch Game and Quit
to Desktop are missing from the main menu. The hooks now take r1 (always in the context, so it's
the one checked) and r23-r29 as arguments (`static_assert` that both games' layouts are within
r23-r29). This works with the options on or off. No other host code reads the context's
non-volatiles (grep `ctx\.r(1[4-9]|2|3[01])` over `port-nx/src`). In the SDK only the save-state
code (`XThread::Save`) and Switch crash reports read them.

### 2.2 SEH funclets take their frame in r12 (patch 0007)
MSVC's 360 code calls a `__finally` funclet inline with the owner's frame in r12
(`addi r12,r31,N; bl funclet`), and the funclet starts with `addi r31,r12,-N`. Under
`non_argument_as_local` both sides' r12 are locals, so the funclet reads 0 and runs on a garbage
frame. F.E.A.R. has 85 such funclets and 42 inline calls (Files: 95 and 48). The scratch translation
shows it: funclet `sub_82884618` comes out as `ctx.r31.s64 = r12.s64 + -128` with `r12` a fresh
local. The SDK already hands a funclet (a `share_registers` function, found from the scope tables)
its owner's localized r14-r31 through the context. Patch 0007 adds r12: a funclet reads r12 from
the context, and the caller writes its r12 there before the call.

Patch 0010: the owner also reaches a split-off inline `__finally` block by a **conditional** branch
(Files: `bge cr6,0x821a989c`, and the block goes on using the owner's r31). The translator emits
that as `if (!cr6.lt) { sub_X(ctx, base); return; }`, which skipped the hand-off. Under
`non_volatile_as_local` the block would read the context's stale r31. The hand-off is now one
helper used by both kinds of call. F.E.A.R. has no such branch; Files has them (section 3).

### 2.3 `_CallSettingFrame` (patch 0008 and the manifests)
The C runtime calls a funclet during unwinding with `mr r12,r4; mtlr r3; blrl`. `skip_lr` drops the
`mtlr`, so the `blrl` called whatever LR last held. Patch 0008 keeps `mtlr` in any function that
contains a `blrl`/`bclrl` (one function per game). The manifests mark `_CallSettingFrame`
`share_registers`, so its r12 (and its non-volatiles) stay in the context for the funclet. This path
runs only while unwinding a guest exception, which today can't happen: the SDK's `RtlRaiseException`
breaks into the debugger instead of unwinding.

### 2.4 setjmp and longjmp (manifests, patch 0009)
The guest's `setjmp` stores r14-r31, f14-f31, CR and LR into its `jmp_buf`. Under the options those
are locals, so the translated guest `setjmp` would save zeros. Today's translated `longjmp` is
already wrong: its final `blr` returns to its own caller. With the addresses set, the translator
emits `ppc_setjmp`/`ppc_longjmp` (host `setjmp`/`longjmp` keyed by the guest buffer's address,
`pch_h.inja`) and copies `ctx` into a local `env` before the setjmp. Patch 0009 also keeps the
localized non-volatiles (and cr2-cr4) in `env`, because a local that changed between the setjmp and
the longjmp is indeterminate afterwards, and the longjmp return takes them back. F.E.A.R. has 10
`setjmp` calls and 24 `longjmp` calls (Files: 10, 24): libpng, libjpeg and the shader compilers'
error paths. The tail calls (`b longjmp`) go through the same special case.

Review points:
- `temp.s64 = ppc_setjmp(r3.u32)` uses setjmp's value in an assignment. ISO C allows it only in a
  comparison or as a statement; MSVC and clang accept the assignment.
- The host `longjmp` unwinds across generated frames. They have no destructors, but check the
  build's `/EH` setting.

## 3. What the scan finds (`tools/analysis/regflow.py`)
The scan follows every path through each generated function (instruction comments, labels,
gotos) and lists the registers the function reads before writing them. Those are values it takes
from its caller: under the options they become locals that start at zero. Reads that only save a
register into the frame (`std r31,-8(r1)`, `mfcr`) aren't counted. Run it with
`python -I tools/analysis/regflow.py port-nx/generated/default [--all]`.

| | F.E.A.R. | Files |
|---|---:|---:|
| functions | 94,608 | 100,703 |
| read a register their caller set | 154 | 204 |
| ... of which CTR, XER carry, reservation or CR | **0** | **0** |

The 154 functions:
- **Expected, and handled above:** `setjmp` itself; the r12 funclets (they also read the owner's
  r14-r31, handled by the SDK's hand-off once the translator marks them); `__chkstk`
  (`sub_8287C854`, size in r12). Under the options `__chkstk` reads 0, probes nothing and returns,
  which is harmless: the guest stack is host memory, and the caller's `stwux r1,r1,r12` uses the
  caller's own r12.
- **Not a dependency:** functions that set up their own frame and read a non-volatile on some path
  before writing it (`sub_82C1F6B0`: r29 on a path where it's dead). A compiler never depends on a
  caller's leftover non-volatiles, so these are dead or infeasible paths.
- **Unreferenced:** frame-less VMX fragments at `828ED650-828EEDC8` (Havok), plus `sub_82ABF0D4`,
  `sub_82340588`, `sub_8246DFD4` and `sub_828C7918`. No direct calls, no pointers to them anywhere
  in the image (`x360img.py ptrs`), no code that builds their addresses.
- **Needs a runtime check (physics):** Havok helpers `sub_828FD958`, `sub_828FE008` and
  `sub_828FE588`, all called from `sub_828F0790`. They read v92 (and v84-v91) with no write
  anywhere on the path, and none of `sub_828F0790`'s four callers touches v92 either. On the 360,
  and in today's build, they read whatever an earlier function left in v92. Under the options they
  read 0. Either the value is dead (the result is masked later), or the VMX128 register field is
  decoded wrongly, or it's a latent bug in the original. Files: `sub_828F3F50`, `sub_828F4600`.
- **Inline `__finally` pieces split off as functions** (Files: `sub_821A989C`, `sub_82893D88`,
  `sub_8288CB54`; none in F.E.A.R.). The owner reaches them with a conditional branch
  (`bge cr6,0x821a989c`), and they go on using the owner's r31. The translator does mark them as
  funclet extents (`sharesRegisters`, scratch run below), and patch 0010 hands them the owner's
  registers.

Reads of a volatile register after a call, without writing it again: 51 in F.E.A.R. Most come after
the varargs error routine `sub_82BEE6E0` (Files `sub_82CDD058`), where a branch target happens to
follow the call, or after `__chkstk`. The rest are single cases, listed by the scan.

**Scratch translation.** Master's translator (`rexgluerd.exe` at `b29317c`, so none of patches
0007-0010) on the branch's manifests, written to a scratch folder (about 2 minutes per game, no
compile):

| | F.E.A.R. | Files |
|---|---:|---:|
| functions that keep r14-r31 in the context (`share_registers`: funclets, their split-off pieces, `_CallSettingFrame`) | 157 | 191 |
| calls into them from functions with local non-volatiles | 319 | 378 |
| ... of which conditional tail calls | 0 | 3 |
| calls that don't hand over a non-volatile the callee reads | 0 | **3**: the three conditional ones (r31). Patch 0010 |
| funclets reading r12 as a fresh local (`r31 = r12 - N`) | all 85 | all 95 (patch 0007) |
| `ppc_setjmp` / `ppc_longjmp` sites | 10 / 24 | 10 / 24 |
| menu hooks emitted with r1 and r23-r30 | yes | yes |

Files' three: `sub_821A9748` to `sub_821A989C`, `sub_82893CE0` to `sub_82893D88`, `sub_8288CB08`
to `sub_8288CB54`. The translator marks all three blocks `share_registers` (they read `ctx.r31`),
so with patch 0010 the owner's r31 reaches them. The check that compares the two translations is
small. It isn't in the repo, but it's easy to redo from `regflow.analyse()` and the
generated code.

## 4. Plan: stages, then A/B on the PC (the Deck only after asking)
Build each stage once, keeping the previous `fear.exe` and `rexruntimerd.dll` as the A side:
1. **Stage A:** `skip_lr`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `cr_as_local`
   (patch 0008). The scan finds no value crossing a call in these.
2. **Stage B:** plus `non_volatile_as_local`, the setjmp/longjmp addresses and the menu hooks
   (patches 0009, 0010).
3. **Stage C:** plus `non_argument_as_local` (patch 0007). It can't come before B (section 1).

For each stage:
- **Measure:** game-thread ms per frame from the profiler (`fear_profile`) in the benchmark room
  and Instant Action Interval 2, uncapped (`fear_vblank_source = "timer"`, 240 Hz guest refresh);
  module totals before and after. Also the compile time and the exe size.
- **Check:** the menu items (PC Settings, Switch Game, Quit); tracers and impacts (#3, #27); AI in a
  firefight; a save and a load; a level start to finish in each game; physics props (Havok, the
  v92 finding); a D3DX texture load.

To build a stage: `git -C third_party/rexglue-sdk-nx checkout register-options`, comment out the
options of the later stages in both manifests, then `tools/build-nx.ps1`. The translator runs
again because the manifest changed.

**Stage A (2026-10-23, F.E.A.R. only).** Manifest at `555a235`, SDK branch at `e42a98c` (patches
0007-0012 in, only 0008 active). The A side is master's build of 09:53 (`8db2298`, SDK
`b29317c`), copied to `private/ab/A-master-8db2298/` (git-ignored). Run each side over the same
stretch of play, with the profiler on and the same frame cap:

    pwsh -NoProfile -File tools/launch.ps1 -Collection -Exe private/ab/A-master-8db2298/fear.exe -Timer -FrameRate 120 -Extra '--nx_renderer native --fear_profile true --fear_profile_hz 200'
    pwsh -NoProfile -File tools/launch.ps1 -Collection -Timer -FrameRate 120 -Extra '--nx_renderer native --fear_profile true --fear_profile_hz 200'

then `python -I tools/analysis/abcompare.py port-nx/logs/<A log> port-nx/logs/<B log>`: game-thread
work in ms per frame (busy time minus the Direct3D wait) and per module, medians over the gameplay
windows, B against A. The A run's log carries the folder name (`run-<time>-A-master-8db2298.log`).

**Stage A result (PC, RTX 4090, 2026-10-23).** Build: 5 minutes, `fear.exe` 81.98 MB to 69.7 MB.
In the generated code `ctx.lr` went from 274,672 references to 3 (the kept `mtlr`/`blrl` in
`_CallSettingFrame`); `ctx.ctr`, `ctx.xer`, `ctx.cr0-7` and `ctx.reserved` to 0. Both runs: Instant
Action Interval 2, 3x scale (3840x2160), the game's MSAA, the PC texture pack, a 120 fps timer cap,
profiler at 200 Hz (logs `port-nx/logs/run-20261023-123222-A-master-8db2298.log` and
`run-20261023-125147.log`, git-ignored):

| | A (master) | B (stage A) | |
|---|---:|---:|---:|
| gameplay windows (10 s) | 46 | 52 | |
| draws per frame, median | 658 | 667 | +1% |
| game-thread work per frame, median | 2.24 ms | 1.95 ms | **-13%** |
| mean / p25 / p75 | 2.37 / 1.82 / 3.00 | 2.13 / 1.57 / 2.57 | -10% / -14% / -14% |
| per 1,000 draws, median | 3.44 ms | 3.02 ms | -12% |
| slow-frame reports | 14 | 8 | |
| GPU per frame, mean | 3.08 ms | 3.04 ms | same |

Per module the `engine` (-25%) and `ClientFX` (-24%) medians moved most; the others sit at the
profiler's resolution at this load (0.17 ms per frame is 1% of a 120 fps window). The user's
read: the smoothest run so far, steadier at 120 than A, no visual glitches (tracers fine), the menu
items present (the log's "added 3 items"). Nothing broke. The GPU's 75-85% "usage" on the 4090 is
the same in both: about 3 ms of GPU time per 8.3 ms frame at 4K with MSAA and the texture pack, at
reduced clocks; the translator's options don't touch it.

Note for runs with `-Collection`: the game waits on the collection menu (window up, nothing logged)
until a game is picked, which looked like a hang in the first B launch.

Stage A on the Deck (`fear_021`, uncapped): game-thread work per frame -17% against `fear_020`,
median 88 fps on the OLED's 90 Hz screen; see `docs/HANDOFF-PERF.md`.

**Stage B (2026-10-23, F.E.A.R. only).** Manifest at `c3498d1`: stage A plus
`non_volatile_as_local` and the setjmp/longjmp addresses; patches 0009-0012 and the
`share_registers` entries now take effect. Built in 3 minutes with no errors; `fear.exe` 69.7 MB to
65.3 MB. In the generated code, context references to r14-r31 went from 930,890 to 3,969 (the
hand-offs and the `share_registers` functions) and to f14-f31 to 2,990. Save/restore helper calls
went from 39,300 to 8, all in Havok's v92 chain, which keeps them (patch 0011). The solver hands
`v92` to `sub_828F0790`, the menu hooks get the locals (`FearMenuAddItems(ctx.r1, r23, ..., r30)`),
and there are 10 `ppc_setjmp` and 24 `ppc_longjmp` sites. Not run yet. The A side is stage A's
build, copied to `private/ab/A-stageA-555a235/`:

    pwsh -NoProfile -File tools/launch.ps1 -Collection -Exe private/ab/A-stageA-555a235/fear.exe -Timer -FrameRate 120 -Extra '--nx_renderer native --fear_profile true --fear_profile_hz 200'
    pwsh -NoProfile -File tools/launch.ps1 -Collection -Timer -FrameRate 120 -Extra '--nx_renderer native --fear_profile true --fear_profile_hz 200'

What to watch beyond the numbers, because this stage is where the review's breakages were:
physics (props, ragdolls, anything flung: the v92 constant), crashes, the three menu items, textures
and effects loading (the setjmp paths), a save and a load.

**Stage B crash on the Deck (`fear_023.log`, `crash-20261023-141856.txt`/`.dmp`, in
`private/deck-logs/`).** About a minute into play after the Docks load (70-85 fps, nothing odd on
screen), an access violation on guest thread `F800004C` (`XThread021C`):
- The stack, symbolized against this build's PDBs (`llvm-symbolizer`): `rexruntimerd.dll+0x50CB8D`
  is `HostToGuestFunction<KeWaitForSingleObject_entry>` reading the wait's timeout argument (r7,
  context offset 0x38) as a guest pointer. Its caller is `sub_82ABD508` (`fear_recomp.176.cpp`, the
  thread's entry), which `XThread::Execute` calls directly. The guest address read was `0x60`.
- `sub_82ABD508` is an XAudio voice thread's loop (`XAudioGetVoiceCategoryVolume`, spin locks,
  `KeReleaseSemaphore`). It waits on a fixed event (`0x833992C8`) with `r6 = 0` and `r7 = 0` before
  each wait. **The compiled code stores both with one `vmovaps %xmm6, (%r14)`** (r14 = `&ctx.r6`)
  and keeps the zero in `xmm6` for the whole loop, because `xmm6`-`xmm15` are callee-saved in the
  Windows x64 ABI. So `xmm6` came back from one of the loop's calls as `{0, 0x60}`, and r7 became
  `0x60`. The whole 16-byte register, including the high half the r6 store doesn't show, was wrong.
- The master and stage A builds compile this function the same way (zero in `xmm6`, the same merged
  store; checked in `private/ab/`), and neither ever crashed there.
- What the loop calls, and why none of it explains this so far:
  - **The game code it calls:** under stage B the translated callees (`sub_82ABCD80`,
    `sub_82ABCC10`, `sub_82ABB7C8`, `sub_82ABBE78`, `sub_82ABC8A8`, `sub_82ABC608`,
    `sub_82AC1978`) **don't touch `xmm6`-`xmm15` at all** (disassembled). It makes no indirect calls,
    and its call tree reaches no setjmp or longjmp site.
  - **The kernel imports it calls:** `KeWaitForSingleObject`, `KeSetEvent`, `KeReleaseSemaphore`,
    spin locks, `KeRaiseIrqlToDpcLevel`/`KfLowerIrql` (which only write a byte) and two XAudio
    getters. They're the same in all three builds: `rexruntimerd.dll` differs between stage A and B
    only in its timestamp and PDB id (15 bytes). None of them runs game callbacks on this thread.
  - **Ruled out:** the SDK writing another thread's context (it only writes the current one's);
    GPU interrupts (they run on the thread that raises them); fibers (host fibers, own stacks); host
    stack overflow (16 MiB per thread); a `{r6 = 0, r7 = 96}` pair anywhere in the game code. The
    SDK's exception handler writes back whole XMM registers, and only the ones it changed.
- **The leading hypothesis is untested:** something asynchronous replaces this thread's `xmm6`
  between the loop's calls. Under Proton that's thread suspension and signal delivery: our profiler
  suspends every busy thread 200 times a second (`SuspendThread`, `GetThreadContext` with
  `CONTEXT_CONTROL`, a stack read, `ResumeThread`), and the audio thread is suspended mostly inside
  `ZwWaitForSingleObject`. It would explain why stage B is the first build hit: until now the
  translated code almost never kept live values in `xmm6`-`xmm15` across a call. Stage B keeps
  f14-f31 and the vector registers there all the time, so a register that gets replaced now carries
  real data (here `{0, 0x60}`, which looks like two 64-bit locals) instead of a stale copy of the
  same zero.
- **Tests that separate it, no build needed:** (1) stage B on the Deck with
  `--no-fear_profile` at the end of the launch options, for a long session; (2) stage B on the
  PC (native Windows), profiler on, for the planned A/B. A crash in (1) means the profiler isn't
  the cause; no crash in (2) but a crash in (1) points at Proton itself. If the profiler is the
  cause, the fix is in `port-nx/src/profiler.cpp`, not in the translator (for example, sample
  without suspending threads under Wine).

**Two more runs (`fear_024.log`, `fear_025.log`, crashes `144032` and `144706`): the same crash,
deterministic.** Meant as test (1), but the profiler was still on: both logs have the `[profile]
sampling busy threads` line. My instruction was wrong. For an on/off cvar, `--fear_profile false`
turns it **on**: CLI11 reads `--fear_profile` as the flag, and the parser stashes the separate
`false` as a positional (`src/core/cvar.cpp`, "a separated --name value pair ... never consumed").
Off is `--no-fear_profile` or `--fear_profile=false`. The Deck README's diagnosis section used the
same wrong form and is fixed (`tools/steamdeck/README-SteamDeck.txt`). Earlier diagnosis runs that
used it (`--fear_vertex_fresh false`, `--nx_native_msaa false`, `docs/HANDOFF-PERF.md`) may not
have run with the setting they meant to. `fear_010` printed no `vertex reads:` line to confirm
either way, and the MSAA run's log isn't in `private/deck-logs/`.

What the three crashes share: 50, 62 and 78 s after the game started, on the same thread, with the
same registers (`rcx = 0x60`, `rbx = 0x283399200`, `r10 = 0x833992C8`, `r12 = 0x1F`, the same `rsp`
and `rbp`). A random interruption wouldn't leave `{0, 0x60}` in `xmm6` three times out of three. So
whatever replaces `xmm6` does it the same way each time. If it's the profiler or Proton, it's
reached through a fixed state (another thread whose `xmm6` holds that value). If it's our code, a
path this analysis hasn't found yet. Test (1) with `--no-fear_profile` and test (2) still decide
which. Stage A, with the profiler on, ran 10 minutes without it (`fear_021`).

**Test (2), stage B on the PC (native Windows, RTX 4090), profiler on: no crash in 15 minutes**
(`port-nx/logs/run-20261023-145530.log`, 14:55-15:10, Instant Action Interval 2, quit normally).
The same XAudio thread (`F800004C`) ran throughout. So the crash is specific to Proton: either
its handling of `xmm6`-`xmm15` (suspension, signals, the syscall path) or the profiler's suspends
under it. Test (1), the Deck with `--no-fear_profile`, separates those two.

Stage B's speed on the PC, same settings as stage A's run (120 fps cap, 3x, MSAA, texture pack),
game-thread work per frame:

| | windows | median | mean | per 1,000 draws | slow-frame reports |
|---|---:|---:|---:|---:|---:|
| master | 46 | 2.24 ms | 2.37 ms | 3.44 ms | 14 |
| stage A | 52 | 1.95 ms | 2.13 ms | 3.02 ms | 8 |
| stage B | 89 | 1.56 ms | 1.62 ms | 2.58 ms | 12 |

Stage B against stage A: -20% median, -13% per 1,000 draws (this run's scenes averaged 611 draws per
frame against 667). Against master: -30% median, -25% per 1,000 draws. GPU time unchanged.

**Test (1), the Deck with the profiler really off (`--no-fear_profile`; `fear_026.log`,
`fear_027.log`, crashes `151551` and `152233`): the same crash.** Neither log has the profiler's
sampling line. Crash signature and registers are identical to the first three. Time from the game
starting to the crash, all five runs:

| Run | profiler | seconds |
|---|---|---:|
| `fear_023` | on | 62 |
| `fear_024` | on | 50 |
| `fear_025` | on | 78 |
| `fear_026` | off | 129 |
| `fear_027` | off | 385 |

The profiler isn't the cause. It may make the crash come sooner, but five runs are too few to say.
All five crashed in ordinary play at 80-90 fps, with no slow frames, loading or log event before the
crash. The user's video of `fear_027` shows 75-89 fps in a firefight up to the crash. The Deck's
Mesa build (`git-035ae2f854`) is the same in every run today, the clean stage A run included. A
Proton update since 13:54 isn't ruled out (it wouldn't change Mesa): the user mentioned SteamOS,
controller and dock firmware updates today.

**Next test: stage A on the Deck again** (`private/steamdeck/FEAR360-SteamDeck-stageA.zip`, the same
`fear.exe` as `fear_021`, copied from `private/ab/A-stageA-555a235/`). A crash would mean today's
updates broke something under every build. No crash pins it on stage B under Proton.

**Result (`fear_028.log`): stage A, profiler off, 35 minutes (15:43-16:19), no crash.** Today's
updates are ruled out: it's stage B under Proton. It's also the steadiest Deck run so far:

| | `fear_021` (stage A, profiler on) | `fear_028` (stage A, profiler off) | `fear_027` (stage B, to the crash) |
|---|---:|---:|---:|
| gameplay windows | 54 | 204 | 33 |
| fps median | 88.0 | 89.9 | 89.1 |
| fps, lowest tenth | 71.8 | 86.3 | 83.9 |
| fps min | 59.2 | 63.3 | 80.7 |
| GPU per frame | 9.8 ms | 9.3 ms | 9.1 ms |

## 7. The proposed fix for the stage B Deck crash (SDK patch 0013, for review, not built)
**What's known.** These hold together:
- On every run, stage B's XAudio voice loop (`sub_82ABD508`) gets `xmm6` back changed from one of its
  calls under Proton.
- It's the same function, compiled the same way, in master and stage A (zero kept in `xmm6`, one
  `vmovaps` stores r6/r7). Those never crash, on the Deck or the PC.
- Under stage B the loop's translated callees don't touch `xmm6`-`xmm15`, and the runtime DLL is
  byte-identical (apart from its timestamp) to stage A's.
- Stage B on native Windows ran 15 minutes clean.

So the loop's own code doesn't change `xmm6`. The register changes while the thread is in a host
call or a system call, under Proton, and that only matters in stage B. The working explanation: under
Proton the thread sometimes gets `xmm6`-`xmm15` back with contents from elsewhere (other threads, or
a stale save area). Until stage B those registers almost never held live values, so the swap went
unnoticed. Stage B keeps f14-f31 and the vector registers there, so now it carries real data, and
`{0, 0x60}` looks like two packed 64-bit locals. The loop is in a system call most of the time
(`ZwWaitForSingleObject` 95-99% of its samples), which fits.

**The fix** (`patches/rexglue-sdk-nx/0013`, SDK branch `register-options` at `490d264`):
`REX_KEEP_CALLEE_SAVED_XMM()` in `include/rex/ppc/function.h`. It's an empty `asm volatile` that
clobbers `xmm6`-`xmm15`, Windows x64 only. A function that contains it has to save those registers
in its prologue and restore them in its epilogue, from its own stack frame, so whatever happens to
them during the function's host call, the caller gets its own values back. It's placed in every host
call the generated code makes that can block or enter the kernel:
- `HostToGuestFunction` (every kernel export, `__imp__*`), after the host call;
- MMIO accesses: `REX_MM_LOAD_*`/`REX_MM_STORE_*` now call `ppc_mmio_load_`/`ppc_mmio_store_`,
  `noinline` helpers in `pch_h.inja`, 46 sites in F.E.A.R.;
- the global lock: `REX_CHECK_GLOBAL_LOCK`/`REX_ENTER_GLOBAL_LOCK`/`REX_LEAVE_GLOBAL_LOCK` now call
  `ppc_global_lock_check_`/`_enter_`/`_leave_`, 89-113 sites.

The loop above reaches only these. Its kernel calls (waits, events, semaphores, spin locks, IRQL,
two XAudio getters) go through `HostToGuestFunction`, and `sub_82AC1978` takes the global lock.
Not covered, because they don't enter the kernel: `REX_QUERY_TIMEBASE` (`QueryPerformanceCounter`,
a shared-memory read under Wine), `ppc_setjmp`/`ppc_longjmp` (host CRT), `REX_CALL_INDIRECT_FUNC`
(a table lookup, then a translated function or a wrapped export) and our midasm hooks (no system
calls).

**Cost.** Each wrapped call saves and restores ten 16-byte registers (20 moves) next to a call that
already costs far more. Native Windows never changes them, so there it's only that.

**What it doesn't cover, and how we'd know.** If the register changes while the thread is running
translated code (a signal at an arbitrary instruction), not during a host call, the patch won't
stop the crash. A Deck run would show that: the same crash again means the change isn't confined to
host calls. Then the options are a diagnostic build that records which call returns with `xmm6`
changed, or keeping stage B off on Proton (stage A only on the Deck).

**For the reviewer:**
1. Is a clobber-only `asm` enough to force the prologue save and epilogue restore under clang-cl
   (MS ABI), and could the compiler keep its own value in `xmm6` across the host call in these
   small wrappers (which the change would then hit)? Disassemble `HostToGuestFunction<...>` after a
   build to confirm the `movaps` saves.
2. Are there other host calls from generated code that enter the kernel that this misses?
3. Is the working explanation (Proton swapping in other XMM contents during system calls) plausible,
   and is there a known Wine/Proton issue to cite? The Deck's Proton version isn't in our logs.

### 7.1 Review of patch 0013 (2026-10-23, third pass, nothing built)
**1. The fix is correct as written.**
- A clobber in an `asm` is a definition of the register to LLVM, and a callee-saved register
  defined anywhere in a function is saved in the prologue and restored in the epilogue (on Win64
  the whole 128 bits, `movaps`). Clang does not elide it for an empty asm. The same function
  already compiles `asm volatile("" ::: "memory")` in this build, so the syntax is known to be
  accepted by clang-cl. All three carriers are `noinline` (`[[gnu::noinline]] inline` is fine:
  `inline` for linkage, `noinline` for codegen), so the save and restore sit in a frame of their
  own around the host call.
- The wrapper's own values are safe. The clobber sits right after the host call: anything live
  across the call and used after the asm cannot be in `xmm6`-`xmm15`, nothing runs between the
  call's return and the asm, and a volatile asm is not reordered across a call. What the patch
  cannot protect is host code *inside* the call (the export body, if inlined into the wrapper, or
  any runtime code) that keeps a value of its own in `xmm6`-`xmm15` across a Win32 call: that is
  the exposure the whole DLL has if the registers really change, patch or not.
- Include order: `pch_h.inja` includes `<rex/ppc.h>` (which includes `rex/ppc/function.h`) at
  line 32 and `<rex/system/mmio_handler.h>` at 38, both before the helpers at 167. The MMIO
  macros' continuation lines are odd-looking but syntactically right; `REX_CHECK_GLOBAL_LOCK`
  still reads the count with the lock held; `u32`/`u64` are in scope.
- **Cost item missed:** `[entrypoint.rexcrt]`'s `memcpy` and `memset` are `REX_HOOK`s too, so
  every call now saves and restores ten vector registers around a copy that was already 1-4% of
  the game thread. Worth a wrapper variant without the clobber for `REXCRT_EXPORT` (a template
  flag on `HostToGuestFunction`). Not done here: untested, and a measurement call.

**2. Coverage.** Covered: every `REX_EXPORT` (through `REX_HOOK`), rexcrt included; the MMIO
accesses; the global lock. Not covered, and why it is fine for now:
- `REX_STUB`/`REX_STUB_RETURN`/`REX_STUB_LOG` exports log on every call (spdlog, a file write):
  none fired in `fear_023`-`fear_027` (no `STUB` line).
- `REX_HOOK_RAW` exports: the eleven printf-family ones in `xboxkrnl_strings.cpp`;
  `DbgPrint`/`XamDbgPrint` log. None in these runs.
- `ppc_trap` (logs), and `REX_CALL_INDIRECT_FUNC`'s miss path (`ResolveIndirectFunction`, which
  locks): only for targets outside the dispatch table.
- The midasm hooks: `fear_menu` and `fx_debug` log (not in this loop); the profiler's are atomics.
- `REX_QUERY_TIMEBASE` is `host_tick_count_platform()`: QPC, which Wine answers with `rdtsc` on an
  invariant-TSC CPU. No system call, as stated.
- **The asynchronous path is outside the patch by design:** the SDK write-watches GPU memory
  (`EnableAccessCallbacks`, `exception_handler_win.cpp`'s vectored handler). Under Wine a watched
  store in translated code is a `SIGSEGV`, `KiUserExceptionDispatcher`, our handler, `NtContinue`.
  That is a context save and restore at an arbitrary instruction, on whichever thread wrote.

**3. The diagnosis.** Wine's x86-64 syscall dispatcher (current `dlls/ntdll/unix/signal_x86_64.c`)
saves the whole FP state into the thread's syscall frame on entry (`xsave64`, or `xsavec64` when
`XState.CompactionEnabled`, mask `XState.EnabledFeatures & 7`; `fxsave64` without XSAVE) and on
the fast return restores `xmm6`-`xmm15` from that area with `movaps 0x1c0(%rcx),%xmm6` ...
`movaps 0x250(%rcx),%xmm15` (`xrstor64` only when the frame's restore flags ask for FP or
XSTATE); the unix-call dispatcher saves and restores them with `movdqa`. So keeping them across a
system call is designed in, and a swap needs a bug in that save or restore. One of exactly that
shape is on record: [Wine bug 58335](https://bugs.winehq.org/show_bug.cgi?id=58335) (10.9-10.10,
mid 2025): `init_xstate_features` left `EnabledFeatures` at 0, `xsave64` with an empty mask
stored nothing, and the return still restored `xmm6` from the unwritten slot, so a function that
kept a value in `xmm6` across a system call got the frame's stale contents back. It hit only CPUs
without AVX (the Deck's Van Gogh has AVX2) and is fixed (`03039ab2`, `e5f591f3`). Nothing is on
record for AVX CPUs or for `NtWaitForSingleObject`. So: plausible as a bug of that shape in the
Proton build on the Deck, unconfirmed; the Proton version is the first thing to learn (the crash
report now prints it, 7.2).

Two refinements of the working explanation:
- "Contents from elsewhere" is more likely **this thread's own stale frame** than another thread's
  registers: the syscall frame is per thread and reused by every system call, so an unsaved slot
  holds the `xmm6` of the last call that did save it. That also explains why the same loop never
  crashed in master and stage A: stage B is the first build whose translated code leaves non-zero
  data in `xmm6`-`xmm15` across system calls, so before it the stale slot held zero, which is
  what the loop expected anyway.
- `{0, 0x60}` is the shape of a `li rN,0; li rN+1,96` pair stored as one vector. Twenty
  translated functions build one (`sub_82345958`, `sub_8243EEA8`, `sub_8245C280`, `sub_825419F0`,
  `sub_82569690`, `sub_825A8118`, `sub_825B0A40`, `sub_825D8660`, `sub_825F0160`, `sub_8262E2F0`,
  `sub_82646A40`, `sub_826A9D68`, `sub_826E4B38`, `sub_826F1338`, `sub_826FAC50`, `sub_827AAAB8`,
  `sub_828544A0`, `sub_82854640`, `sub_82A7C0E8`, `sub_82CC94D0`), but **none is in the voice
  loop's call tree** (seven translated functions). So the value came from host code on that
  thread, or from another thread's context after all. A diagnostic build settles it.

What a diagnostic build should record, if the patched build still crashes:
1. A checking variant of `REX_KEEP_CALLEE_SAVED_XMM` in `HostToGuestFunction`: copy
   `xmm6`-`xmm15` to a 160-byte stack buffer before the host call (`movaps` in an asm), compare
   after, and on a mismatch log once per export: the export's name, thread id, the register, both
   values, `MXCSR`, and `wine_get_version()`. The same in the MMIO and lock helpers.
2. In the vectored handler: a per-thread count of write-watch faults, printed in the crash report
   for the faulting thread. A crash with no mismatch from (1) and faults on that thread points at
   the exception path; no mismatch and no faults points at signals or thread-context changes.
3. On the Deck, no build needed: `PROTON_LOG=1` (its log header names the Wine build), a run on a
   different Proton (9.0 against 10 or Experimental), and `PROTON_NO_FSYNC=1 PROTON_NO_ESYNC=1`,
   which moves the waits onto a different system-call path.

**4. Section 7 otherwise.** "Every kernel export" means every `REX_EXPORT`; the stubs and raw hooks
above are outside. "Our midasm hooks (no system calls)": two of them log. The loop description,
the counts and the cost statement hold. None of patches 0007-0013 has been compiled.

### 7.2 What changed on the branch in this pass
`port-nx/src/crash_report.cpp`: the report text now prints `xmm6`-`xmm15`, `MXCSR` and, under
Wine, `wine_get_version`/`wine_get_build_id`, so the next Deck crash shows the register and the
Proton build without the dump. Patch 0013 and the manifests are unchanged.

### 7.3 Built (2026-10-23 17:43): stage B with patch 0013, F.E.A.R. only
- **The crash report didn't compile.** Its `"\n"` escapes had become real line breaks, the same
  backslash mangling seen in this repo's tooling before. Fixed in `605b1f5`, which also moves the
  register lines above the `Stack:` heading.
- **Forcing the translation.** The translator step depends only on the executable and the manifest
  (`generated/default/codegen.d`), not on the translator, so patch 0013's template change needed
  the stamp deleted (`generated/default/codegen.build.stamp`). Then one file was regenerated,
  `fear_pch.h`, with the MMIO and lock helpers. Worth fixing in the SDK's CMake: the rule should
  depend on the `rexglue` binary.
- **Result:** no other errors. `fear.exe` 65,228,800 bytes, `rexruntimerd.dll` 12,870,144 (about
  100 KB larger, from the saves in every wrapper).
- **Checked in the disassembly:**
  - `HostToGuestFunction<KeWaitForSingleObject_entry>` saves `xmm6`-`xmm15` (`vmovaps` to
    `0x30`-`0xc0(%rsp)`) before calling `xeKeWaitForSingleObject` and restores all ten before
    `ret`.
  - `ppc_global_lock_enter_` in `fear.exe` has the ten saves too.
- The Deck update zip (`private/steamdeck/FEAR360-SteamDeck-update.zip`) has this build. F.E.A.R.
  Files is unchanged (09:53).

### 7.4 Deck result: no crash (`fear_029.log`)
Nine minutes on the Deck with the profiler on (17:53-18:02), and no crash. Without patch 0013, the
same setup crashed within 50-78 s three times out of three. So patch 0013 fixes it: the register
changes happen during host calls, as the review's stale-save-area reading predicted, and saving
`xmm6`-`xmm15` around those calls is enough. What changes them in Proton is still unidentified.
The crash report's new lines will show it if anything slips through.

Stage B's Deck speed: game-thread work per 1,000 draws 12.37 ms (stage A, `fear_021`) to 11.00 ms
(-11%); heavy scenes' minimum 59 to 74 fps, median 65 to 80. Patch 0013 costs about 0.1 ms per frame
in the runtime DLL, partly from the native `memcpy`/`memset` paying the saves. Details in
`docs/HANDOFF-PERF.md`.

Open:
- the clobber-free wrapper for the rexcrt hooks (7.1);
- F.E.A.R. Files at stage B (its manifest already has the review's `share_registers` entries);
- stage C (`non_argument_as_local`, patch 0007);
- the translator rule that ignores the translator's own changes (7.3).

## 5. Questions for the reviewer
1. The SDK's funclet hand-off copies only the r14-r31 that were already referenced earlier in the
   function, in address order (`locals.r[i]`). A non-volatile live at a funclet call but first
   referenced at a higher address would be missed. In the scratch translation every direct call
   hands over every register its funclet reads (section 3), but copying all 18 would be simpler and
   safe.
2. The v92 reads in Havok: real, dead, or a VMX128 decode problem in the translator?
3. `skip_lr` costs the SDK's guest backtraces (crash reports, `ctx.lr`). Worth it?
4. Patch 0008 scans the whole function for `bclrl` on every `mtlr`. That's quadratic per function
   in the worst case, though a function has few `mtlr`. Fine?
5. Anything else that reads the context's registers from host code, or depends on a register
   crossing a call outside the ABI, that the scan can't see (indirect calls, jump tables)?

## 6. Review (2026-10-23, second pass, nothing built)
Checked against the code, today's translations (`port-nx/generated/default`,
`port-files/generated/default`) and a fresh scratch run of master's translator (`b29317c`,
`rexgluerd.exe codegen`, 2:00 and 2:17) on the branch's manifests. Two findings are blockers for
`non_volatile_as_local` and are fixed on the branch; the rest are answers and smaller corrections.
The checks below are small Python over the generated code (classify a function as
`share_registers` by `ctx.r14`-`ctx.r31` **with a field access**, since a localized function's
hand-off lines also mention `ctx.r31`; walk `sub_X(ctx, base)` calls for the call graph).

### 6.1 Havok passes a constant in v92 (question 2): real, fixed
`sub_828FD958`, `sub_828FE008` and `sub_828FE588` read v92 and never write it. Every call path
into them runs through `sub_828F0790`, whose only callers are the four big solver functions
`sub_828F0DA0`, `sub_828F2840`, `sub_828F4FC8` and `sub_828F7018` (1,700-2,550 instructions
each). Each of those **computes** v92 before the call, with the same idiom:
`vspltisw v0,1; vspltisw128 v94,0; vcuxwfp128 v93,v0,0; vor128 v0,v93,v93; vsubfp128 v0,v94,v0;
vrlimi128 v0,v93,1,0; vor128 v92,v0,v0` (v93 = 1.0, v94 = 0, v92 a lane mask of +-1). No other
function in the image writes v92 except the `__savevmx` helpers and `sub_828ED5E8` (the
unreferenced fragment routine below, which sets v92-v94 the same way). So on the 360 the three
callees always see the solver's constant through the callee-saved register, and with the option on
they read 0: the dot products `vmsum4fp128 v12,v92,v12` (stored with `stvewx`) go wrong in the
constraint solver. Files: the same with `sub_828E6D88` and its callees `sub_828F3F50`,
`sub_828F4600` (writers `sub_828E3BE0`, `sub_828E7398`, `sub_828E8E38`, `sub_828EB5C0`).

Not a decode problem: v92 is the 8th most common `vA` of `vmsum4fp128` image-wide (73 sites) and
never appears as an operand of `vmsum3fp128` or `vmulfp128`; the other 70 sites are inside the
writers. The three readers also leave v88/v91 (v86/v89 too in two of them) loaded but unread: the
usual sign of code compiled as one unit and split by the compiler's outlining, not of a bad field.

**Fix:** `sub_828F0790` and the three readers are `share_registers` in the manifest (Files: its
three), so they keep every register in the context, and two translator changes make that work:
- **Patch 0012:** the hand-off at a call into a `share_registers` function copies **every**
  non-volatile (r14-r31, f14-f31, v14-v31, v64-v127) into the context and takes them all back,
  instead of the r14-r31 referenced earlier in the caller. That also answers question 1 (yes:
  a register first written at a higher address is live at the call through a loop, and a funclet
  may write one the owner reads later). 118 copies each way, at 49 sites in F.E.A.R. and 50 in
  Files, all `bl` (Files: plus the six conditional branches of 6.3).
- **Patch 0011:** see 6.2. Without it the four functions' prologues (`bl __savegprlr_N`,
  `__savevmx_N`) would be dropped and their callers would take back changed non-volatiles.

### 6.2 A `share_registers` function's save/restore helpers were dropped: fixed
Under `non_volatile_as_local` the translator dropped every call to `__savegprlr_N`,
`__restgprlr_N` and the fpr/vmx helpers, whatever the caller, and emitted the helper bodies with
locals of their own. For a localized caller that is right. A `share_registers` function keeps its
non-volatiles in the context, so without its prologue and epilogue it returned with r14-r31
changed, and its localized caller took those back. In today's images no SEH funclet uses the
helpers (F.E.A.R.: 0 of the 92 share functions; Files: 13 of 122 end in `b __restgprlr_N`, but
all 13 are unwind-entry copies with no callers), so this only bit the functions marked in the
manifest: the v92 chain (6.1), whose members all start with `mflr r12; bl __savegprlr_N`.
**Patch 0011:** the helper nodes share registers, so their bodies save and restore the context's
registers, and a call to them is dropped only in a function whose non-volatiles are locals.

### 6.3 Files: six split-off `__finally` blocks, not three, and the translator does not mark them
Section 3 says the translator marks `sub_821A989C`, `sub_82893D88` and `sub_8288CB54`
`share_registers`. The scratch run says otherwise: all three come out **localized**
(`PPCRegister r31{}` declared, `lwz r3,88(r31)` and `addi r1,r31,160` on a zero r31), and so do
three more of the same shape found by the scan: `sub_82876988`, `sub_82891440`, `sub_82B983F0`
(`mr r8,r8; addi r12,r31,N; bl funclet; ...; b __restgprlr_N`, each reached once by a conditional
branch, e.g. `cmplwi cr6,r27,64; bge cr6,0x821a989c`: a loop's exit, an ordinary path, not an
exception). All six were already in Files' manifest as plain tail-call targets (`= {}`, from the
first translation), which is why they are functions at all and why the funclet sweep, which only
covers a funclet's own extent, never marks them. **Fix:** those six entries are now `share_registers`; with patch 0010
the owner hands its registers over at the branch, and with patch 0011 their `b __restgprlr_N`
restores the context from the owner's frame (harmless either way: nothing is taken back after a
tail call). F.E.A.R. has none: its only entry-reads without a frame are setjmp, the v92 readers,
the fragments and `sub_82BC0D34` (dead `r28`, no callers).

### 6.4 Answers to section 5
1. **Copy all 18:** done, and f/v too (patch 0012, above).
2. **v92:** real, a solver constant passed through a callee-saved register (6.1).
3. **`skip_lr`:** keep it. Host code that reads `ctx.lr`: the function dispatcher's return
   sentinel (`0xBCBCBCBC`, nothing checks it), `xboxkrnl_modules.cpp`'s caller address in a log
   line, and the Switch-only crash report, thread dump and perf overlay (`exception_handler_switch`,
   `threading_switch`, `switch_perf`), plus `XThread::Save`. On Windows and Linux nothing
   functional reads it, and a host backtrace already names the generated functions.
4. **Patch 0008's scan:** fine. 27,554 `mtlr r12` + 10 others in F.E.A.R., each scanning its own
   function once: a few million word reads in a two-minute run. The pointer arithmetic is valid
   (`data` is the section's memory, a function never spans sections, `fn.end()` exists).
5. **Other readers of the context, blind spots:** none in host code. `port-nx/src` reads no
   register beyond the hook arguments (grep of `ctx`/`ctx->` for r0, r2, r11-r12, r14-r31, lr,
   ctr, xer, cr, f0, v32-v63 is empty; `fear_menu.cpp` is the only user of
   `current_ppc_context`). Every other midasm hook takes `PPCRegister&` and so gets the local
   (the anim hooks write `f0` through it: still right). The SDK runtime reads r0, r2, r11, r12,
   ctr, cr, xer only in `XThread::Save/Restore` (save states, unused) and the POSIX fault
   handler (host registers, unrelated). `generate_exception_handlers` is off, so the generated
   SEH catch-all (which reads `ctx.r31`) is never emitted. Indirect calls and jump tables need
   nothing extra: the scan is per callee, and a `bctr` table entry outside the function is
   emitted as a call. Blind spots that remain: the scan's instruction model (two false positives
   fixed below; read-modify-write VMX128 forms it does not list, such as `vmaddcfp128`, would
   show up as reads of a destination, i.e. more findings, not fewer), and code reached only by
   computed addresses (6.5).

### 6.5 Other checks and corrections
- **Section 3's counts** of functions that keep r14-r31 in the context (157 / 191) are inflated:
  the check matched the hand-off lines (`ctx.r31 = r31;`) of localized callers. Real: **92 / 122**
  (85 / 95 r12-funclets, `_CallSettingFrame`, and swept pieces), called from localized functions at
  49 / 50 sites, every one with a hand-off.
- **The unreferenced fragments** `sub_828ED650`-`sub_828EEDC8` start at `sub_828ED5E8`, the
  constants setup (v92-v94, no prologue, no callers), i.e. one hand-written VMX routine the
  analyser cut at its branch targets. If anything reaches it (a computed address the image scan
  cannot see), every fragment loses the v78-v94 it inherits from the previous one. A cheap
  precaution would be `share_registers` on all 33 (Files: 46, `sub_828E3C48`-`sub_828E4D28`);
  not done, since nothing shows it is reached. Watch for a physics crash with these addresses.
- **`regflow.py` false positives fixed:** `xor r30,r30,r30` (the zeroing idiom reads nothing) and
  `stvlx128 v64,r1,r6` (a frame save with r1 as the first operand). They accounted for Files'
  `sub_821A8464`, `sub_821A8930`, `sub_821A8CC0`.
- **Reads before writes in framed functions** (28 / 39, "nonvol, own frame"): uninitialized
  variables (`mr r23,r26` and the like). On the 360 they read whatever was left; now 0. Neither is
  defined behaviour; accepted.
- **setjmp:** the 360's `jmp_buf` holds no VMX registers (neither game's `setjmp` has a vector
  store), so the guest cannot rely on v14-v31 across a longjmp and patch 0009 is complete. `setjmp`
  stores 0 at offset 312 (the frame field `longjmp` tests), so the guest `longjmp` never unwinds and
  a plain host `longjmp` matches it. The host side: the recomp sources are built with `/EHa`
  (`generated/rexglue.cmake`), the generated frames hold only trivial locals; the MSVC CRT's
  `longjmp` unwinds only when its own `_setjmpex` recorded a frame. Unverified until built.
- **Already broken today, not new:** `mtlr r5; blr` at the end of the guest `longjmp` and
  `mtlr r3; blr` in the CRT's context restore are plain returns in every translation.
- **`__chkstk`** with r12 = 0: `neg; addi 4095; srawi. 12` gives 0 and `blelr` returns. Harmless,
  as stated.
- **No second copy of the save/restore helpers** in either image (searched for their store
  patterns): the elision covers every call.
- **Menu hooks:** the scratch run emits `FearMenuAddItems(ctx.r1, r23, ..., r30)` and
  `FearMenuShowItems(ctx.r1, r31)`; the `static_assert` holds for both layouts; the owner's
  locals are untouched by the guest calls the hook makes.
- **Patches 0007-0010** read correctly (fmt strings, const-ness, `byte_swap` in namespace
  `rex`); none has been compiled, nor have 0011-0012.

### 6.6 What changed on the branch in this pass
`patches/rexglue-sdk-nx/0011`, `0012` (SDK branch `register-options` at `e42a98c`, made in a
temporary worktree; the checkout is still `b29317c`); `share_registers` entries in both manifests
(6.1, 6.3); `tools/analysis/regflow.py` (6.5); this section. The stage plan (section 4) stands,
with patches 0011-0012 and the new manifest entries belonging to stage B.
