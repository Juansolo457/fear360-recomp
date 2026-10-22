// F.E.A.R. 360 - model animation time at any frame rate.
//
// The engine steps model animations in whole milliseconds (LithTech's trk_Update): each frame
// the client turns its frame time into msDelta = (uint32)(seconds * 1000), and every animation
// tracker then steps by (uint32)(msDelta * rate). Both drop the fraction. At the console's 30 fps
// that costs about 1% (33.3 -> 33 ms a frame), but at 80 fps a frame is 12.5 -> 12 ms (4% slow),
// and an animation slowed down to fit an effect's length gets nothing at all: the bullet-impact
// puffs (Stone_bullethole_*: a puff model whose "Base" animation is stretched over the effect,
// rate ~0.05) step 12 * 0.05 = 0.6 -> 0 ms every frame. The puff then stays in the compact first
// frame of its expansion while the effect's scale keys grow it: dark, oversized "rocks" sticking
// out of the wall that only vanish when the effect ends, and a dark haze when you walk into one
// (play-test 4). At 30 fps (33 * 0.05 = 1.65 -> 1 ms) it plays, slowly.
//
// With fear_anim_exact_time (default on) the dropped fractions are kept, per model and per
// tracker, and added back the next time, so animations advance by exactly the time that passed.
// The hooks are called by the generated code just before each truncation (fear_manifest.toml,
// [[midasm_hook]]), with the model or tracker (r3) and the value about to be truncated (f0).

#pragma once

#include <rex/ppc/context.h>

// msDelta = seconds * 1000 in the model animation update (sub_822415D0, sub_821E4DA8).
void FearAnimFrameMs(PPCRegister& r3, PPCRegister& f0);
// msDelta * rate in trk_Update (sub_82191E40).
void FearAnimTrackerMs(PPCRegister& r3, PPCRegister& f0);
