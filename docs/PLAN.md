# F.E.A.R. Xbox 360 Static Recompilation: Roadmap

Goal: run the Xbox 360 versions of **F.E.A.R.** and **F.E.A.R. Files** (*Extraction Point* + *Perseus Mandate*) natively on Windows and Linux. That means no full-system emulator, unlocked frame rate, gamepad and TV ("10-foot") UI out of the box, and game data supplied by the user from their own discs.

Approach: **static recompilation** of the PowerPC `default.xex` into C++, the same way [UnleashedRecomp](https://github.com/hedge-dev/UnleashedRecomp) did with [XenonRecomp](https://github.com/hedge-dev/XenonRecomp). It is not a matching decompilation. A decomp can come later, one function at a time, by swapping recompiled functions for hand-written source.

---

## Why recomp and not decomp first

| | Static recomp | Matching decomp |
|---|---|---|
| Time to something playable | Months | Years |
| Output | Machine-translated C++ that is faithful but ugly | Human-readable source |
| Mods and fixes | Hooks or mid-asm patches on specific functions | Edit the source directly |
| Compatible with the other | Yes: decompiled functions can replace recompiled ones over time | n/a |

Recomp first gives a playable build early. Functions that need real changes (frame timing, renderer, input, resolution) get hand-rewritten as we go.

## What we know about the target (to verify in Phase 1)

- **Engine:** Monolith **LithTech Jupiter EX**. Day 1 Studios did the 360 port (2006). *F.E.A.R. Files* (2007) bundles both expansions on one disc. Check whether your expansion discs are *Files* or separate releases.
- **PC reference material:** Monolith shipped a **F.E.A.R. Public Tools / SDK** with source for the game-side code (`ClientShellDLL`, `ObjectDLL`, shared game code). That is a huge help for naming and understanding recompiled functions, because most gameplay logic should match the 360 build closely. The engine core (renderer, physics, sound) was not released.
- **Expected layout:** on PC the engine and game are split into DLLs. On 360 they are most likely statically linked into one `default.xex`, which suits recomp. Each title (base game, Files) is probably its own XEX, so we expect **two recomp targets** sharing one runtime.
- **360-specific parts to replace:** Xenos GPU command buffers and microcode shaders, XMA audio, XAM UI/profiles/saves, Xbox Live (stub it out), and possibly `Instant Action` and other 360-only modes, which are worth preserving.

> Sanity check: the PC version of F.E.A.R. already runs on Windows and under Proton on Linux. This project is worth doing for the 360 builds' controller-first UI and console-specific content, and for the craft of it. Keep that in mind when setting scope.

---

## Phases

### Phase 0: Acquire game data (you, with your discs)
See [RIPPING.md](RIPPING.md). Output: a clean ISO of each disc, with hashes recorded in `docs/DUMPS.md` (hashes only, never the files).

### Phase 1: Recon of the XEX
- Extract `default.xex` and the data files from each ISO. Decrypt and decompress the XEX (XexTool or XenonRecomp's own loader).
- Load it in a disassembler (IDA / Ghidra with the XEX + PPC plugins). Record: the import list (which `xboxkrnl.exe`/`xam.xex` functions it calls), sections, size, and whether any symbols, debug strings or asserts survive.
- Boot it in **Xenia** (canary) to see how well it runs. Xenia's logs tell us which kernel calls and GPU features the game actually uses, which is our runtime to-do list.
- Match strings and code shapes against the PC SDK source to start a symbol map (`symbols/*.toml` or similar).
- **Exit criteria:** we know how many XEX targets there are, the import list, and Xenia's compatibility status.

### Phase 2: First recompile
- Run `XenonAnalyse` to find jump tables and function boundaries, then iterate on the TOML config (missing functions, `setjmp`/`longjmp` addresses, `savegprlr`/`restgprlr` helpers, mid-asm hooks).
- Run `XenonRecomp` to produce the C++, then build it with clang. Expect many rounds of fixing function-boundary and switch-table problems.
- **Exit criteria:** the generated code compiles and links against a stub runtime.

### Phase 3: Runtime, the "hybrid" layer
Pick a base, a decision to make together once we have your repo list:
1. **UnleashedRecomp-style** custom runtime: hand-written kernel/XAM shims, a native renderer (D3D12 + Vulkan through plume / RHI), SDL for input and windowing. Fastest at runtime, most work.
2. **Xenia-derived runtime** for the recompiled code: reuse Xenia's kernel HLE, its GPU command processor (Xenos → D3D12/Vulkan translation), and its XMA decoder, without the CPU JIT. Boots sooner, with some per-frame translation overhead. This is the hybrid approach you described. Several newer recomp toolchains work this way, e.g. ReXGlue, which the GoldenEye 360 PC build used. We'll evaluate it alongside whichever repos you send.

Sister project: `../quake4-360-recomp` has the same pipeline and much better reference source, so it's the likely first target. Runtime pieces that work there carry over to this one.

Either way, the work items are:
- Memory: the 4 GB guest address space, big-endian loads and stores (handled by the recomp), physical/virtual mirrors
- Kernel and XAM: threads, events, critical sections, file I/O (ISO/XContent mounting), user profile and save stubs, Live stubbed out
- GPU: ring buffer / PM4 parsing, or hooks at the engine's renderer layer. Shaders are recompiled ahead of time with **XenosRecomp**
- Audio: XAudio2 → SDL/miniaudio, XMA → PCM (FFmpeg or Xenia's decoder)
- Input: XInput → SDL3 gamepad. Works with any controller on Linux, which is the "on a TV" requirement
- **Exit criteria:** boots to the main menu.

### Phase 4: Playable
- Fix crashes level by level and test full playthroughs of all three campaigns.
- Saves go to a user directory, and settings get a native config file.

### Phase 5: Enhancements
- **Frame rate:** find the engine's timestep (LithTech uses a frame-time delta, so likely mostly OK). Hunt down the code that assumes 30 fps (physics substeps, animation, particle spawn rates, UI timers) and patch it.
- Native resolution and widescreen, MSAA, aspect-correct HUD, ultrawide maybe
- Gyro/rumble through SDL, remappable controls, optional mouse and keyboard
- Linux: AppImage / Flatpak, Steam Deck testing

### Phase 6 (optional, long-term): Gradual decompilation
Replace recompiled functions with readable C++, starting from the PC SDK game code, and track progress per function.

---

## Repo layout (planned)

```
fear360-recomp/
├── docs/              plan, ripping guide, findings
├── tools/             helper scripts (extraction, hashing, symbol import)
├── thirdparty/        submodules: XenonRecomp, XenosRecomp, SDL, etc.
├── config/            XenonRecomp TOML per title (fear.toml, fear_files.toml)
├── runtime/           shared kernel/XAM/GPU/audio/input layer
├── ppc/               GENERATED recompiled code (gitignored)
├── game/              YOUR dumped game files (gitignored, never committed)
└── CMakeLists.txt
```

## Legal ground rules
- **No game data or generated code from game data goes in the repo.** That covers ISOs, XEX files, assets, and `ppc/` output. Users supply their own dumps, like UnleashedRecomp's installer that reads the user's own files.
- Only use the PC SDK source as reference under its EULA terms. Don't copy it wholesale into a distributed build.

## Open questions / next actions
1. **You:** send the GitHub repos you've been looking at (recomps and hybrid runtimes). They'll decide the Phase 3 base.
2. **You:** confirm exactly which discs you have (F.E.A.R. 360, F.E.A.R. Files, separate expansions?) and which dumping method fits (see RIPPING.md).
3. **Us:** once a `default.xex` exists, start Phase 1 recon.
