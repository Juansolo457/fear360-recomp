// F.E.A.R. 360 - a sampling profiler for where the CPU time goes (#28: the Steam Deck can't hold
// 60 fps in big fights, and the renderer isn't what limits it).
//
// With fear_profile (off by default) a thread samples every busy thread of the process
// fear_profile_hz times a second: it suspends the thread, reads its instruction pointer and
// resumes it. A sample in translated game code is put under its guest function (sub_XXXXXXXX, from
// the generated PPCFuncMappings table: the nearest native function start below the address), a
// sample anywhere else under its module (rexruntimerd.dll is the SDK's kernel and runtime, ntdll
// and the like are waits and the OS). Every 10 s the log gets, per busy thread (by the name the
// SDK gives guest threads), its CPU use (GetThreadTimes) and its top functions, and the top
// functions over all threads: "[profile] ..." lines. The guest functions name the code to look up
// in the PC version (private/pc_ref/bin) and to rewrite natively if they are the cost.
//
// Under each thread's top functions (#30), two more lines:
// - "modules": its samples by part of the game, from the map of the executable in
//   docs/ARCHITECTURE.md (engine, engine renderer, game client, game server, AI planner, Havok,
//   Direct3D, ClientFX, C runtime, ...; one table per game), and outside the translated code by
//   DLL ("ntdll.dll") or the exe (the SDK's and our host code). A sample taken while the game's
//   Direct3D waits for the GPU counts as "Direct3D wait" instead.
// - "Direct3D waits": those samples by what Direct3D waited for. Direct3D marks the start and end
//   of every wait (FearD3DWaitBegin/End below) with a reason code; the names follow the XDK's
//   D3DBLOCKTYPE order, which matches what the call sites wait for (1: room in the ring buffer,
//   2: the secondary ring, 3: the swap throttle, 6: a fence, the odd numbers 7-15: resources
//   the GPU still uses). "untracked" is reason 0, which Direct3D doesn't count either.
// - "system calls from": samples in ntdll, win32u, kernelbase or kernel32 by who made the call:
//   "ntdll.dll!ZwDeviceIoControlFile < HID.DLL!... < rexruntimerd.dll!... < fear.exe <
//   sub_821CFCD8 9%" (up to four other modules on the way out, each once, innermost first, then
//   the first translated function, from the return addresses on the thread's stack; a scan of the
//   stack, so it can name a stale caller, the translated function most of all). Export names are
//   the nearest named export, so a DLL's internal code (SDL inside rexruntimerd.dll) gets a
//   neighbour's name: read the module, not the function.
//
// Windows (and Proton on the Deck: Wine implements the thread calls); a no-op elsewhere.

#pragma once

#include <rex/ppc/context.h>

namespace fear::profiler {

// After the runtime is up (ReXApp::OnPostSetup). Starts the sampler when fear_profile is on.
void Start();

}  // namespace fear::profiler

// Manifest hooks on Direct3D's wait bookkeeping: at the start of the function that records a
// wait's start (sub_82973A50 in F.E.A.R., sub_82973648 in Files; the reason in r5) and of the one
// that records its end (sub_82973A80, sub_82973678). They only note the reason for the sampler,
// and do nothing while fear_profile is off.
void FearD3DWaitBegin(PPCRegister& r5);
void FearD3DWaitEnd();
