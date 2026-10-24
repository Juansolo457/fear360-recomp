// F.E.A.R. 360 - fixes for game code that misbehaves at high frame rates, after EchoPatch's
// "High FPS Fixes" for the PC version (github.com/Wemino/EchoPatch). The 360 build is the PC
// game's code, so it has the same problems once it runs above the console's 30 fps. Each fix is a
// [[entrypoint.midasm_hook]] in fear_manifest.toml (F.E.A.R.) and port-files/fear_manifest.toml
// (F.E.A.R. Files), and the tracking issue is #2 in the repository.
//
// AI updates (issue #3, EchoPatch's "Degraded AI reactions"). An AI asks the engine to update it
// again in 0.01 s (CCharacter's SetNextServerUpdate: the engine's set-next-update for its object,
// and the delay kept in the object). Below 100 fps a frame is longer than that, so the AI updates
// every frame; at 120 (8.3 ms frames) it updates every other frame, and its timers (perception,
// reactions, decisions) advance by one frame's time per update, so AI time runs at about half
// speed: enemies "caught off guard … still very accurate but kind of slow". With fear_ai_every_frame
// (default on) the AI's 0.01 becomes a delay shorter than any frame, so it updates every frame, as
// EchoPatch does. A delay of 0 (stop updating) is left alone: on the 360 a third caller passes it.
//                       F.E.A.R.        F.E.A.R. Files   PC GameServer.dll (1.08)
//   SetNextServerUpdate sub_8263D2A0    sub_8266F620     0x10009220
//     the hook (f1)     0x8263D2B8      0x8266F638
//     delay kept at     +0xB34          +0xC8C           +0xA74
//   0.01 callers (bl)   0x826480C0,     0x8267B480,      0x10013382 (update),
//                       0x826481B8      0x8267B578       0x1001347E (initial update)
//   0.0 caller (bl)     0x82647760      0x8267AA40
// EchoPatch also leaves a character at 0.01 once a limb is severed, because a dismembered body
// updated every frame moves erratically. The hook reads CCharacter's m_bSevered for that: the
// death code (CCharacter's, as in the PC SDK) sets it to 1 when DamageTracker::CheckForSever
// returns true, just before PrepareToSever and the sever-frequency counter.
//                       F.E.A.R.        F.E.A.R. Files   PC GameServer.dll (1.08)
//   CheckForSever       sub_826E4418    sub_8273E2D0     0x100EB600
//     called at         0x825B4138      0x825FF604
//   m_bSevered          +0x680          +0x714
// At the hook, r3 is still the character (SetNextServerUpdate's this; mr r31,r3 comes next).
//
// Particle batch markers (issue #7, EchoPatch's "Oversized particles"; the likely cause of the haze
// under lights, #38). After each batch it emits, a ClientFX particle system puts a marker in its
// particle list: a particle whose total lifetime holds the batch's update time and whose lifetime
// is -1, so the next update finds it expired and removes it. Until then it is drawn like any other
// particle: CParticleSystemGroup::GetParticleSizeAndColor takes its age as 1 - lifetime / total,
// 1 + 1 / frame time, and the scale and colour curves extrapolate past their last key in a
// straight line. So every emitting system draws one sprite far bigger than its particles, 31
// times past the end of life at 30 fps, 121 times at 120: a soft glow sprite near a light becomes
// a haze, four times larger at 120 fps than on the console. With fear_particle_marker_fix (default
// on) the marker's lifetime is 0, as EchoPatch sets it: its age is 1, a particle at its end of
// life, and it is still expired at the next update. The hooks are at the store of the -1.0 (lfs f0
// from a constant shared by ~290 other places, then stfs f0 to +0x18), in AddParticleBatchMarker
// and in its copy inlined at the end of EmitParticleBatch (the PC SDK's
// ClientFxDLL/ParticleSystemGroup.cpp has the same code).
//                          F.E.A.R.      F.E.A.R. Files
//   AddParticleBatchMarker sub_82BB65E0  sub_82B7BA50
//     the hook (f0)        0x82BB6634    0x82B7BAA4
//   EmitParticleBatch      sub_82BB6E40  sub_82B7C2A8    (calls AddParticleBatchMarker)
//     the hook (f0)        0x82BB7424    0x82B7C88C
//   GetParticleSizeAndColor sub_82BB7440 sub_82B7C8A8

#pragma once

#include <rex/ppc/context.h>

// At the start of SetNextServerUpdate: the character in r3, the delay still in f1.
void FearAiNextUpdate(PPCRegister& r3, PPCRegister& f1);

// At the store of a particle batch marker's lifetime: the -1.0 in f0.
void FearParticleBatchMarker(PPCRegister& f0);
