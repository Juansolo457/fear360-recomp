// F.E.A.R. 360 - the screen bloom's strength and threshold (#38).
//
// The 360 version adds a bloom the PC version doesn't have (CBloomMgr, the FX/Bloom database
// record): each frame the scene copy is shrunk to a quarter (bloom_downsample: a pixel whose
// colour has any channel over the threshold is kept whole, else black), blurred with two 15-tap
// passes (bloom_blurh, bloom_blurv) and added back at the given strength (bloom_blend:
// scene + bloom x strength). Strength and threshold come from the console variables ScreenBloom
// and ScreenBloom_Threshold, read through sub_823C7360 (name in r3, the value to use when the
// variable doesn't exist in f1): 0.2 and 0.8 when nothing sets them, as on the rooftop.
//
// It was the haze under lights (#38): a lit wall over the threshold spread a grey glow over the
// dark sky beside it, weaker when bullet dust dimmed the wall, gone with the strength at 0. The
// PC version has no bloom (no trace of it in the SDK's game code or in 1.08's GameClient.dll), so
// it is off by default, matching the PC; PC Settings can turn the 360's back on.
//
// FearBloomParams, at the point both have been read and before they go to the bloom material,
// replaces them: fear_bloom_intensity (0 = off, the default; below 0 = the game's) and
// fear_bloom_threshold (below 0 = the game's, the default).
//                            F.E.A.R.      F.E.A.R. Files
//   the hook (f1 threshold,  0x823C7908    0x82390ED4
//             f31 strength)
//   the lookup helper        sub_823C7360  sub_82390900

#pragma once

#include <rex/ppc/context.h>

void FearBloomParams(PPCRegister& f1, PPCRegister& f31);
