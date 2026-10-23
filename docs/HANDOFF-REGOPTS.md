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
| `port-nx/fear_manifest.toml`, `port-files/fear_manifest.toml` | Seven options on (`skip_lr`, `ctr_as_local`, `xer_as_local`, `reserved_as_local`, `cr_as_local`, `non_argument_as_local`, `non_volatile_as_local`), `skip_msr` left off; `setjmp_address`/`longjmp_address`; `share_registers` on the C runtime's `_CallSettingFrame`; the two menu hooks take more registers |
| `port-nx/src/fear_menu.cpp`, `fear_menu.h` | The menu hooks take r1 and r23-r29 as arguments instead of reading them from the thread's context |
| `patches/rexglue-sdk-nx/0007-0010` | Four translator fixes. They are also commits on the SDK branch `register-options` in `third_party/rexglue-sdk-nx`; the SDK checkout itself is back at master's `b29317c`. Each one changes the output only when its option is on, so master's translation is unchanged |
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
