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
// Windows (and Proton on the Deck: Wine implements the thread calls); a no-op elsewhere.

#pragma once

namespace fear::profiler {

// After the runtime is up (ReXApp::OnPostSetup). Starts the sampler when fear_profile is on.
void Start();

}  // namespace fear::profiler
