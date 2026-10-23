# How F.E.A.R. 360 runs here, and what it would take to run like the PC version

Applies to `default.xex` SHA-256 `e7fccdf6dd57bb58d10efbdd0b2ca567b8cece5b6a3fc696c7fca879aaea2792` (title `565507D9`). Steam Deck numbers are from `fear_006.log` and `fear_007.log` (builds `db180a0` and `7d3a36c`, about 130 ten-second profile windows; the logs are git-ignored under `private/deck-logs/`). Written 2026-10-23 from static analysis of the image, the translated code, the SDK source, the PC game DLLs and the Deck logs. Nothing here was built or run. Each claim says whether it was **measured**, **read** (in code or data) or **inferred**.

## Short answers

- **Is it just the PowerPC?** Partly. The game's own code is translated one PowerPC instruction at a time and keeps every 360 detail: big-endian memory (a byte swap on every load and store), the 32 registers held in a struct in memory instead of CPU registers, condition-register bookkeeping, calls to the 360's register save/restore helpers, and a stack-cookie check called from 2,407 places. Every register option the translator has is off for this game (see [The translated code](#the-translated-code)). The game's logic is effectively single-threaded: in the Deck profiles one thread does client, server, AI, physics, effects and the Direct3D work, and no other game thread is busy. The 360 version ran at 30 fps on one Xenon hardware thread (it waits for two vblanks per frame, `docs/FINDINGS.md`). 60 fps needs roughly twice that throughput, through the translation.
- **Is it the renderer?** Not in the big fights, now. In windows below 55 fps the game thread is busy for its whole frame (about 19 ms median), and the renderer thread needs 4 to 8 ms (**measured**). The GPU is the second limit: 13 ms median and up to 17.7 ms per frame there, with the game's own MSAA honoured. But the GPU *interface* costs CPU on both sides. The game builds Xenos command packets in its own copy of Direct3D. We parse them back into draws. And when the game gets ahead, its Direct3D spins on the CPU until we catch up.
- **Why can't we just do it the way the PC version does?** The PC version is different code, not just different hardware. On PC, `FEAR.exe` (the LithTech engine and its D3D9 renderer) loads `GameClient.dll`, `GameServer.dll` and `ClientFX.fxd`. The 360 executable links all of that into one image, together with Havok, the Xbox 360 Direct3D and D3DX libraries (including a full HLSL compiler), XACT, Bink, XUI and zlib/libpng/libjpeg. And it's a later branch of the game code: 1,042 of the 1,137 C++ classes in the PC base game's `GameClient.dll` and `GameServer.dll` also exist on 360, and 408 more classes exist only on 360 (**read**, RTTI). There's no engine source for either version, and the PC SDK's game source is a different revision. So "do it like PC" means rewriting parts of the 360 code natively, piece by piece, not swapping in PC code.
- **What would we rewrite from the ground up?** Not the game. In order of value: the GPU interface (replace the 360 Direct3D library with native calls, as Unleashed Recompiled did); the platform pieces that cost time under Proton (controller rumble, timers, spin-waits); then individual game subsystems, only where a profile shows a concentrated cost. The profile today is flat: no game function takes more than about 4% of the game thread. The biggest CPU lever doesn't need hand-written code: switching on the translator's register options (below).

## The two versions side by side

| | PC (Ultimate Shooter Edition + EchoPatch) | Xbox 360 (this project) |
|---|---|---|
| Executables | `FEAR.exe` engine (x86) + `GameClient.dll`, `GameServer.dll`, `ClientFX.fxd` | one PowerPC image, `FEAR_F.pe`, 94,373 functions, 3.1 M instructions |
| Game code revision | 1.08 era; public modding SDK has GameClient/GameServer/ClientFX source | later branch: 92% of PC classes plus 408 more (XUI scenes, `PitFight*` classes that look like Instant Action, many `CAIActionTarget*` AI classes, E3 demo menus) |
| Graphics | D3D9 API and its HLSL shaders | 360 Direct3D library, statically linked, builds PM4 packets for the Xenos GPU; shaders are Xenos microcode; D3DX effects and an HLSL compiler ship in the image |
| Physics | Havok | Havok (build string "Day1_FEAR_VUG_Xbox360-PS3_HavokPhysics") |
| Audio, video, UI | not compared here | XACT (`snd\fear.xgs`), Bink, XUI |
| How we run it | DXVK/D3D9 under Proton on the Deck | translated C++ (ReXGlue) + the SDK's 360 kernel + fear-nx's native Vulkan renderer that reads the PM4 stream; Proton on the Deck |
| Frame rate | 100-120 fps on the user's PC; game logic per frame, AI updates fixed by EchoPatch | 120 fps on the user's PC; 45-60 on the Deck (60 needed) |

## Map of the 360 executable

From RTTI vtables (1,454 class names, 7,682 functions labelled), referenced strings and call patterns (**read**; scripts in `tools/analysis/`). Link order puts each library in one block:

| Range | Module | Functions | Instructions |
|---|---|---:|---:|
| `82170000-8227FFFF` | LithTech engine runtime: client/server managers, world, models, resources, UDP networking, files, Havok wrapper (`LthkWorld`) | 11,738 | 268,080 |
| `82280000-822CFFFF` | Engine renderer: materials, shader libraries, DrawPrim, dynamic vertex/index buffers | 3,760 | 80,219 |
| `822D0000-822EFFFF` | Engine sound (XACT) | 782 | 32,040 |
| `822F0000-8250FFFF` | Game client: `CGameClientShell`, HUD, menus/XUI, client weapons, client FX objects | 16,085 | 544,790 |
| `82510000-826BFFFF` | Game server: `CGameServerShell`, player, characters, weapons, world objects, AI nodes and nav-mesh links, `CAI` | 12,581 | 433,478 |
| `826C0000-8280FFFF` | AI planner: sensors, goals, actions, activities, A* | 16,270 | 333,553 |
| `82810000-8286FFFF` | Shared game code: game database, rules, console, animation trees | 3,527 | 96,220 |
| `82870000-8289FFFF` | C runtime: `memcpy` `8287D240`, `memset` `8287B540`, stack-cookie check `8287C878`, string functions, register save/restore helpers `8287B4xx` | 1,165 | 47,900 |
| `828A0000-8295FFFF` | Havok physics | 4,851 | 194,355 |
| `82960000-829BFFFF` | Xbox 360 Direct3D (August 2006 XDK): state, resources, PM4 command building, GPU waits, PIX hooks | 2,242 | 96,869 |
| `829C0000-82ABFFFF` | D3DX: HLSL and effect compilers, plus zlib, libpng, libjpeg | 3,535 | 259,725 |
| `82AC0000-82BDFFFF` | ClientFX effects library (no RTTI; identified by its property strings) | 7,603 | 290,586 |
| `82BE0000-82D4FFFF` | XGraphics shader microcode compiler | 7,090 | 372,270 |
| `83040000-830FFFFF` | Static initialisers, Bink video, misc | 3,144 | 60,325 |

About a third of the image (`829C0000-82D4FFFF`) is shader compilers and image libraries that only run at load time, if at all.

## How a frame flows (Deck threads, from the profiler)

1. **Main XThread** (the game): input, client update, server update (AI, physics through Havok), ClientFX, then the engine renderer walks the scene and calls the 360 Direct3D. Direct3D writes PM4 packets into the ring buffer in guest memory. When the GPU (us) falls behind, Direct3D spins.
2. **GPU ring native** (fear-nx renderer, `port-nx/src/native/`): reads PM4, rebuilds draw state, translates Xenos shaders to SPIR-V (cached, pre-warmed), copies and hashes vertex/index data (spans, #27), handles EDRAM resolves and tiled textures, records Vulkan. It writes the GPU's "done" values (fences, read pointer, swap counter) back into guest memory, and the game polls them.
3. **Audio Worker**: XACT/XMA.
4. **UI thread** (thread 332): SDL event pump, run once per controller poll from the game.
5. **SDK timer queue**: a spin-waiting worker (13% of a core).
6. Wine's `wine_xinput_hid_update`, file and other system threads.

Total CPU in fights: 1.5 to 2.1 cores (**measured**, `fear_007`). Only the main thread is ever the limit.

## Where the Deck's frame time goes

The main thread is busy about 89% of the time in every window: it never idles, because when it's ahead it spins in Direct3D (**measured**). Splitting each window into "Direct3D waiting" and "everything else" (`tools/analysis/waitshare.py`):

| Windows | Count | fps (median) | Draws/frame | Game thread work per frame | Direct3D wait | Renderer CPU | GPU (median / max) |
|---|---:|---:|---:|---:|---:|---:|---:|
| Below 55 fps | 22 | 50.3 | 1,071 | **19.0 ms** | 1% (0-19%) | 4.6 ms | 13.0 / 17.7 ms |
| At 60 fps | 78 | 60.0 | 604 | 12.7 ms | 24% (6-38%) | 3.7 ms | 12.4 / 14.8 ms |

(Windows with fewer than 100 draws per frame, the menus and loading screens, are left out. "Direct3D wait" counts the spin loop, its caller and its timer read; the register save/restore helpers the loop also calls are counted as work, so the wait is slightly understated.)

So the big fights are **CPU-bound on the game thread**, with the GPU close behind. In those windows the profile's top entries (`fear_007`):

- `ntdll!ZwDeviceIoControlFile` 8-10%, `ZwCreateEvent` 1-2%, `ZwClose` 1-2%: **controller rumble**, see below.
- Engine renderer and ClientFX functions (`sub_82BCAFF0`, `sub_822A6CD0`) 2-4% each; `memcpy` 1-4%; register save/restore helpers 1-3%; Havok broadphase and AI functions around 1% each.
- Then a long, flat tail: the profiler prints only the top 8 per thread, so most of the 19 ms isn't attributed yet. Per-module totals (Tier 1) fix that.

## Finding 1: the game's Direct3D spins while it waits for the GPU

**Read** (disassembly), **measured** (share). The hottest main-thread function across all windows, `sub_82973C88` (13.5% average), is the body of a spin loop in the 360 Direct3D. Four iterations of eight `or r31,r31,r31` (a Xenon hardware-thread priority hint, a no-op once translated), then a 5000-tick timeout check against a timer read through `r13+0x100`. It's called in a loop by the waits:

| Function | Waits until | Reason code passed to `sub_82973A50` | Inferred meaning |
|---|---|---|---|
| `sub_82989490` | ring read pointer (`*(device+0x2A10)+0x3C`) passes the space needed | 1 | command ring full |
| `sub_829893B8` | swap counter (`*(device+0x2A10)+4`, low 2 bits) | 2 | frame throttle at present |
| `sub_8298A208` | fence (`*(device+0x2A10)+0`) reaches a value | 6 | block on fence (global device) |
| `sub_8298A2F0` | same fence check; kicks the ring first if the fence isn't submitted (`sub_8298A988`) | caller's | block on fence |

`device+0x2A10` points at the block of values the GPU writes back. Here our ring thread writes them. In 60 fps windows the wait is the frame limiter's idle time (24% median), spent spinning at full clock. On the Deck that's power the GPU could use, and an SMT sibling slowed down. In the first loading window it's about 70% of the thread. In the big fights it's near 0: the game thread is the bottleneck there, so removing the spin saves power and helps the other threads, but doesn't shorten a slow frame by itself.

## Finding 2: controller rumble blocks the game thread on the Deck

**Read** (SDK, SDL and Wine source) plus **measured** syscalls. Inferred link: needs one Deck test to confirm.

- The game sets vibration through XInput. The SDK's `InputSystem::SetState` calls `SDLInputDriver::SetDeviceVibration`, then `SDL_RumbleGamepad`, on the game thread.
- For the Deck's built-in controller, SDL's HIDAPI Steam Deck driver sends rumble as a synchronous HID feature report on the calling thread (`HIDAPI_DriverSteamDeck_RumbleJoystick` calls `SDL_hid_send_feature_report`). SDL only sends when the value changes. In fights it changes constantly.
- Under Proton that's `HidD_SetFeature`, whose helper `sync_ioctl` (Wine `dlls/hid/hidd.c`) does `CreateEventW`, `DeviceIoControl`, `GetOverlappedResult` and `CloseHandle` every time: exactly the `ZwCreateEvent` / `ZwDeviceIoControlFile` / `ZwClose` trio the profiler sees on the main thread. The ioctl is a round trip through wineserver and winebus to the controller.
- Cost: 11-14% of the game thread in fights, about 2-3 ms per frame (**measured** share; the cause is the inference above).
- **Test without a build:** turn vibration off in the game's options, play the same fight, and read the profile. If the trio disappears, moving rumble to its own thread in the SDK keeps rumble and gets the time back.
- Also: every `GetState`, `GetCapabilities`, `SetState` and `GetKeystroke` calls `RefreshDevices` (all drivers enumerate), and each SDL poll posts `SDL_PumpEvents` to the UI thread (26% of a core). SDL's joystick lock is shared, so the game thread can wait for the UI thread's HID reads. The SDL Deck driver also re-sends its lizard-mode watchdog every 200 updates.

## The translated code

**Read** (manifest, SDK `src/codegen/`, generated code). The shape of a translated function, with made-up names and values (the generated code itself is never committed):

```cpp
DEFINE_REX_FUNC(sub_XXXXXXXX) {
	REX_FUNC_PROLOGUE();
	ctx.r11.s64 = HI << 16;                                      // lis r11,HI
	ctx.r11.u64 = REX_LOAD_U32(ctx.r11.u32 + LO);                // lwz: guest memory, byte-swapped
	ctx.cr6.compare<uint32_t>(ctx.r3.u32, ctx.r11.u32, ctx.xer); // cmplw: full CR6 update
	if (ctx.cr6.eq) return;                                      // beqlr
	sub_YYYYYYYY(ctx, base);
	return;
}
```

Every register is a field of `ctx` in memory, so the host compiler can't keep values in registers across instructions that might alias, or across calls. What the generator offers, and what this game uses:

| Option (`fear_manifest.toml`) | F.E.A.R. | Unleashed Recompiled (`SWA.toml`) | Effect |
|---|---|---|---|
| `non_volatile_as_local` (r14-r31) | off | on | callee-saved registers live in host registers; the save/restore helper calls are dropped (`builders/context.cpp`) |
| `non_argument_as_local` (r0, r2, r11, r12, f0, v32-63) | off | on | scratch registers become locals |
| `cr_as_local`, `ctr_as_local`, `xer_as_local`, `reserved_as_local` | off | on | flags, loop counter, carry and reservation become locals |
| `skip_lr`, `skip_msr` | off | on | no link-register or MSR bookkeeping on every call |
| `setjmp_address`, `longjmp_address` | unset (found: `82BBA450`, `82BBA200`) | set | needed once registers are locals; used by the D3DX image and shader-compiler code |
| `[rexcrt]` native CRT | unused | (n/a) | `memcpy`, `memset`, `memmove`, string and heap functions run as host code; Condemned 2's ReXGlue recomp maps the heap this way |

Other per-instruction costs seen in the generated code: 37,630 `disableFlushMode()` calls and 3,250 unconditional MXCSR switches between VMX (flush-to-zero) and FPU code; byte swaps on every guest load and store; `std::fma` for every multiply-add (inline since `-march=x86-64-v3`).

These options are all-or-nothing per build, and they change how every one of the 94,373 functions behaves, so they need a careful A/B (Tier 2).

## Finding 3: the 360 Direct3D is a few hundred entry points, but not a clean API

**Read.** Code outside the Direct3D block calls 333 distinct Direct3D functions: the engine 133, game client 108, D3DX 95, the XGraphics compiler 82, ClientFX 81. About 17 functions outside the block appear to write PM4 headers themselves (engine `82174980-821798B0`, ClientFX `82B63980-82BBBD80`, including the particle batcher `sub_82BB65E0`; the constant scan also matches float constants, so this needs checking one by one). And many 360 D3D "calls" are header inlines that write straight into the device struct, so a native replacement has to read the device's shadow state at draw time, not just hook functions.

## Options, ranked

Effort is for one person working with Claude; every build and Deck run is the user's call.

### Tier 0: Deck tests, no build
1. **Vibration off** in the game's options → confirms Finding 2.
2. **`--nx_native_msaa false`** → GPU headroom in the big fights (fear-nx ships with MSAA off on Switch).
3. Both with `--fear_profile true --fear_profile_hz 200`, same fight as `fear_007`.

### Tier 1: small changes, days
1. **Rumble off the game thread** (SDK SDL driver: a latest-value mailbox and a worker thread). Recovers 2-3 ms per heavy frame if Finding 2 holds.
2. **Profiler: per-module totals** (the table above, per thread), **Direct3D wait reasons** (hook `sub_82973A50`/`sub_82973A80`), and **caller attribution for system calls** (walk the host stack to the first translated function). The next Deck run then says where the 19 ms goes.
3. **`[rexcrt]` memory and string functions.** The SDK strips each guest original it replaces, so every address must be exactly that function. Identified so far from disassembly (**read**; check each against the SDK hook's semantics before use): `memcpy` `8287D240`, `memset` `8287B540`, `_stricmp` `82881970`, `strncpy` `8287CAA0`, `wcslen` `8287C3C0`, `wcscmp` `8287F2B0`, `wcsncpy` `8287C3E8`. Others seen but not in the SDK's list: `strncat` `8287CB00`, `free` `8287D1E0`, `rand` `8287DAD0`, `atoi` `8287DB38`, `isdigit` `8287CCA8`, `_getptd` `8287BB20`, the stack-cookie check `8287C878`, `sin`/`cos`-shaped math at `8287F578`/`8287F650`, a `sprintf`-family function at `8287E558`. Low risk; `memcpy` alone is 1-4% of the game thread.
4. **Wait instead of spin** in the Direct3D wait loop: the ring thread signals a host event when it writes the fence, read pointer or swap counter; a hook on `sub_82973C88` waits on it with a short timeout. Saves power and an SMT sibling at 60 fps; the game's behaviour is unchanged.
5. **SDK timer queue**: replace the disruptor spin (13% of a core) with a blocking wait. **UI thread**: pump SDL at a fixed rate instead of once per poll.

### Tier 2: the translator's register options, 1-3 weeks
Turn on the eight options above, set `setjmp_address = 0x82BBA450` and `longjmp_address = 0x82BBA200` (**read**: the CRT pair that saves and restores f14-f31, r13-r31, LR and CR at the same `jmp_buf` offsets; called only from the D3DX image and shader-compiler code, not from game logic), and check the midasm hooks still see the right registers (they take `PPCRegister&`). C++ exceptions aren't a new risk from `skip_lr`: the image has throw paths (`RtlRaiseException`, `RtlUnwind`), but the SDK's `RtlRaiseException` doesn't unwind guest C++ exceptions today (it breaks into the debugger), so a throw already can't work. Validate with deterministic A/B runs (same save, same inputs), the tracer and AI checks from #3 and #27, and a full playthrough. This is the largest CPU lever available without hand-written code, and Unleashed Recompiled ships with all of it on. The gain for F.E.A.R. isn't measured yet. Measure it on PC first with the profiler.

### Tier 3: native Direct3D layer (Unleashed style), weeks to months
Replace the 360 Direct3D library's entry points with host code that records Vulkan directly: no PM4 building in the game thread, no PM4 parsing in ours, no fence spin. Keep fear-nx's shader translation (microcode → SPIR-V), resolves and texture handling, behind a new front end. Hard parts: the inline device-state writes, the 17-odd direct PM4 writers, EDRAM resolve semantics the engine relies on, and keeping it testable against the current path. Start by measuring the Direct3D module's share of the game thread (Tier 1, item 2). If it's small, this tier mainly buys renderer-thread time and GPU efficiency, not game-thread time.

### Tier 4: native rewrites of hot subsystems, months each
Function-level replacement: a native C++ version of a subsystem is hooked over the translated functions, and both are checked against each other. Use the PC SDK source as a reading aid for the 92% of classes both versions share (contributors get the F.E.A.R. Public Tools themselves; it isn't redistributable, so it never goes in this repo). Pick targets from the per-module profile, not before. A full OpenCE-style decompilation of 94,000 functions is years of work and isn't the plan.

### Not chosen
- Running game logic at 30 Hz with interpolation: halves AI/physics cost but changes the feel; the PC version updates per frame.
- Lowering the frame target: the bar is 60 fps in big fights.

## How other projects did it

- [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp) ([XenonRecomp](https://github.com/hedge-dev/XenonRecomp), [XenosRecomp](https://github.com/hedge-dev/XenosRecomp)): static recompilation with every register option on, the game's Direct3D replaced by a native renderer, shaders recompiled ahead of time, pipelines compiled during loading.
- [Lost Odyssey Recomp](https://github.com/freefrank/LostOdysseyRecomp): ReXGlue with the SDK's Xenia-derived GPU emulation (like our `xenos` path).
- [Condemned 2 Recomp](https://github.com/psxrestore/Condemned2Recomp): ReXGlue, same Monolith engine family; maps the guest heap to native through `[rexcrt]`.
- [Deadly Premonition Recomp](https://github.com/LittleBitUA/DPRecomp): ReXGlue with a native Direct3D 12 renderer in preview next to the emulated path.
- [fear-nx](https://github.com/infriker/fear-nx): this executable on Switch with the native Vulkan renderer we use; about 30 fps, 25-27 in heavy scenes, MSAA off.
- [OpenCE](https://github.com/OpenCommunityEdition/OpenCE): Halo CE (original Xbox) decompiled by hand to C with SDL3/OpenGL: the full-source route.
- [EchoPatch](https://github.com/Wemino/EchoPatch): the PC standard we match; its fixes are tracked in #2 and #4-#26.

## Helping out

The project is public, and contributions are welcome. You need your own copy of the game: nothing from the game (executable, assets, generated translation, logs) ever goes in the repo (see `CLAUDE.md`).

- **Start here:** `docs/PROGRESS.md` for state, `docs/HANDOFF.md` to build and run, `docs/HANDOFF-PERF.md` for the Deck work, this file for the map.
- **Analysis scripts** (`tools/analysis/`, Python; run with `-I` except where noted): `x360img.py` (strings, pointers, PowerPC disassembly; needs `capstone`, run with `-P`), `rtti_map.py` (class names for functions from vtables), `regions.py` (what each 64 KB of code is), `xcalls.py` (who calls into a range), `mostcalled.py`, `containing.py` (which function holds an address), `genscan.py` (sizes, strings, callees, PM4 builders), `waitshare.py` (Deck profile windows).
- **Good first tasks:** Tier 1 items 2, 3 and 5; identifying more CRT functions for `[rexcrt]`; checking the 17 direct PM4 writers; naming hot functions with RTTI and PC SDK knowledge.
- **Testing:** Steam Deck results with the profiler on are as valuable as code. `tools/steamdeck/README-SteamDeck.txt` has the launch options.
